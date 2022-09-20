/*
 *  squelch.c - Basic implementations of various FM squelch algorithms
 *
 *  Copyright (c)2022 Patrick McDonnell <patrick@w3axl.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <filter/filter.h>
#include <filter/sample_buf.h>
#include <filter/complex.h>
#include <filter/dc_blocker.h>

#include <app/app.h>

#include <config/engine.h>

#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

#define SQL_MSG(sev, sys, msg, ...) MESSAGE("SQUELCH", sev, sys, msg, ##__VA_ARGS__)

#define NR_SAMPLES 1024

#define SQL_MAX 2000
#define SQL_SMOOTH 0.5

/* Starting Globals */
static unsigned samplerate = 0;
static unsigned sql_mode = 0;
static unsigned sql_level = 5;
static int in_fifo = -1;
static int out_fifo = -1;
static char truncate_silence = 0;
static char suppress_output = 0;
static char print_debug = 0;

/* FIR highpass filter */
static int16_t *filter_coeffs = NULL;        /* Int16 filter coeffs array */
static size_t nr_filter_coeffs = 0;          /* Size of the filter array */
static struct polyphase_fir *hpfir = NULL;   /* Highpass FIR for noise squelch */

/* Coefficients for FIR Highpass filter with stop-band of 3500 and pass-band of 4000 */
double hpf_coeffs_12k[] = { 1.5584662332383325e-21,5.427393716672668e-06,-2.3243021132657304e-05,1.1183839895825805e-19,0.00015437717956956476,-0.0003212937153875828,3.3852275578865277e-18,0.0010956734186038375,-0.0018515291158109903,3.380739069087662e-18,0.0046401359140872955,-0.0069726635701954365,8.649661402163887e-18,0.014577680267393589,-0.02050068788230419,1.6144567088675172e-17,0.039694104343652725,-0.0559057742357254,2.3112900350583163e-17,0.13085311651229858,-0.2721134126186371,0.33333516120910645,-0.2721134126186371,0.13085313141345978,2.3112900350583163e-17,-0.055905781686306,0.03969409316778183,1.6144563779952722e-17,-0.020500682294368744,0.01457767840474844,8.6496622293445e-18,-0.006972667761147022,0.004640133585780859,3.3807388622925087e-18,-0.0018515285337343812,0.001095673767849803,3.385227764681681e-18,-0.0003212936280760914,0.0001543772959848866,1.118385540546229e-19,-2.324273737031035e-05,5.427200903795892e-06,1.5584662332383325e-21 };
double hpf_coeffs_16k[] = { -6.5822916894831e-07,4.4969875949837086e-21,9.429295459995046e-06,4.651412360265842e-20,-5.0012375140795484e-05,1.3963747072902472e-19,0.00016808522923383862,-1.3798665087091616e-18,-0.0004515335022006184,8.545128830396759e-19,0.0010446899104863405,-1.6771297850568837e-18,-0.0021634085569530725,2.9497450895181854e-18,0.004111547954380512,-4.733236239278975e-18,-0.00730621162801981,7.01368343899121e-18,0.012336397543549538,-9.678628589246163e-18,-0.020126793533563614,1.2513076281903032e-17,0.0324372872710228,-1.522132599158636e-17,-0.05364722013473511,1.7473492299745715e-17,0.09979074448347092,-1.896741191887976e-17,-0.31615251302719116,0.49999964237213135,-0.31615251302719116,-1.8967413573240985e-17,0.09979074448347092,1.7473492299745715e-17,-0.05364722013473511,-1.522132599158636e-17,0.0324372872710228,1.2513076281903032e-17,-0.020126791670918465,-9.678632725149226e-18,0.012336392886936665,7.013681784629985e-18,-0.007306211162358522,-4.7332354120983625e-18,0.004111548885703087,2.949745916698798e-18,-0.002163409488275647,-1.677130922430226e-18,0.0010446907253935933,8.545125728469462e-19,-0.0004515336186159402,-1.3798654747333959e-18,0.00016808536020107567,1.396374836537218e-19,-5.00122805533465e-05,4.651440148364545e-20,9.429262718185782e-06,4.4967331400101205e-21,-6.5822916894831e-07 };
double hpf_coeffs_20k[] = { -5.04288948377507e-07,2.645731938592674e-21,4.654988060792675e-06,6.753271463821875e-06,-1.341389179287944e-05,-3.905207267962396e-05,1.0163967559578549e-19,0.00010553650645306334,0.00010070658754557371,-0.00015061954036355019,-0.00035472316085360944,6.492759313803158e-19,0.0007018938777036965,0.0005926831508986652,-0.0007963005336932838,-0.001705068745650351,2.2927422055420704e-18,0.0028692837804555893,0.0022604551631957293,-0.0028521502390503883,-0.005769688170403242,5.5278859062918236e-18,0.008809749037027359,0.006663785316050053,-0.008116543292999268,-0.015940677374601364,9.947648403963722e-18,0.02340324968099594,0.01757539063692093,-0.021490370854735374,-0.043000973761081696,1.3957443646442088e-17,0.07051049172878265,0.05993451550602913,-0.09191156178712845,-0.3013980984687805,0.6000003218650818,-0.3013980984687805,-0.09191156178712845,0.05993451550602913,0.07051049172878265,1.3957443646442088e-17,-0.043000977486371994,-0.021490370854735374,0.017575392499566078,0.02340325340628624,9.947647576783109e-18,-0.015940675511956215,-0.008116541430354118,0.006663783453404903,0.008809749037027359,5.527885079111211e-18,-0.0057696872390806675,-0.002852149773389101,0.002260454697534442,0.0028692844789475203,2.292743239517836e-18,-0.0017050692113116384,-0.0007963007083162665,0.0005926833255216479,0.0007018940523266792,6.492762932718338e-19,-0.000354723451891914,-0.00015061955491546541,0.00010070660209748894,0.00010553659376455471,1.0163966267108842e-19,-3.905177072738297e-05,-1.3413786291494034e-05,6.753287379979156e-06,4.654896656575147e-06,2.6459353006231526e-21,-5.04288948377507e-07 };
double hpf_coeffs_24k[] = { -3.844478158043785e-07,1.7731983620686205e-21,2.5393778741999995e-06,5.643806161970133e-06,-6.378321848736773e-20,-1.8571487089502625e-05,-3.01547406706959e-05,7.93776797596149e-20,7.005011866567656e-05,0.00010193629714194685,-8.192116579292684e-19,-0.00020140365813858807,-0.00027502336888574064,2.037828922195838e-18,0.00048887322191149,0.0006382971769198775,-4.424679259232913e-18,-0.0010497424518689513,-0.00132482941262424,1.8735295526420336e-18,0.002052097115665674,0.0025222161784768105,-3.045806194439428e-18,-0.0037281340919435024,-0.004488963168114424,4.56014249224766e-18,0.0064026364125311375,0.007593767251819372,-2.4749389511374397e-17,-0.010571172460913658,-0.012427469715476036,8.254359232050308e-18,0.017136316746473312,0.02015301026403904,-1.0088413864704933e-17,-0.02822515182197094,-0.03381172940135002,1.1619126477485337e-17,0.05101900175213814,0.06558297574520111,-1.263688536726752e-17,-0.13613708317279816,-0.274813175201416,0.6666663885116577,-0.274813175201416,-0.13613708317279816,-1.2636886194448132e-17,0.06558298319578171,0.05101900175213814,1.1619126477485337e-17,-0.03381172940135002,-0.02822515182197094,-1.0088414691885546e-17,0.02015301026403904,0.017136313021183014,8.25435675050847e-18,-0.012427471578121185,-0.010571173392236233,-2.4749399437541748e-17,0.007593766786158085,0.006402634549885988,4.5601433194282724e-18,-0.0044889613054692745,-0.0037281345576047897,-3.045804953668509e-18,0.002522216411307454,0.0020520968828350306,1.873530173027493e-18,-0.0013248290633782744,-0.0010497428011149168,-4.4246796728232196e-18,0.0006382976425811648,0.0004888733965344727,2.037828508605532e-18,-0.000275023456197232,-0.00020140355627518147,-8.192112960377504e-19,0.00010193629714194685,7.005004590610042e-05,7.937736956688519e-20,-3.015455513377674e-05,-1.8571603504824452e-05,-6.378317971327652e-20,5.643831627821783e-06,2.5392901079612784e-06,1.7731983620686205e-21,-3.844478158043785e-07 };

/**
 * @brief Integer-based square root
 * 
 * Borrowed from https://stackoverflow.com/questions/1100090/looking-for-an-efficient-integer-square-root-algorithm-for-arm-thumb2
 * 
 * @param value input value
 * @return uint32_t output integer square root
 */
static uint32_t square_root(uint32_t value) {
    uint32_t op = value;
    uint32_t res = 0;
    uint32_t one = 1uL << 30;

    while (one > op) {
        one >>= 2;
    }

    while (one != 0) {
        if (op >= res + one) {
            op = op - (res + one);
            res = res + 2 * one;
        }
        res >>= 1;
        one >>= 2;
    }

    if (op > res) {
        res++;
    }

    return res;
}

/**
 * @brief Return the unitless power value of an array of samples
 * 
 * this is just sqrt(sum(abs(samples))) but it seems to work well
 * 
 * @param samples pointer to array of samples
 * @param size length of array
 * @return uint16_t RMS value in uint16 format
 */
static uint16_t get_pow(int16_t *samples, uint16_t size) {
    uint32_t sum = 0;
    for (uint16_t i=0;i<size;i++) {
        sum += abs(samples[i]);
    }
    return (uint16_t)(square_root(sum));
}

/**
 * @brief Translates mode integer to string name
 * 
 * @param mode mode integer
 */
static const char* _mode_name(int mode) 
{
    switch (mode) {
        case 0:
            return "CSQ";
    }
    /* Default */
    return "Unknown";
}

/**
 * @brief Prints usage of command
 * 
 * @param appname 
 */
static void _usage(const char *appname)
{
    SQL_MSG(SEV_INFO, "USAGE", "%s -S [sample rate] -M [mode] -L [level] -o [out_fifo] [in_fifo]", appname);
    SQL_MSG(SEV_INFO, "USAGE", "        -L      (optional) Squelch sensitivity level (0-10, default 5)");
    SQL_MSG(SEV_INFO, "USAGE", "        -M      (optional) Squelch mode (0 = CSQ is it for now)");
    SQL_MSG(SEV_INFO, "USAGE", "        -o      (optional) output fifo instead of stdout");
    SQL_MSG(SEV_INFO, "USAGE", "        -P      (optional) Print squelch debug info to stdout");
    SQL_MSG(SEV_INFO, "USAGE", "        -S      Samplerate in Hz (valid samplerates are: [12000 16000 24000 48000]");
    SQL_MSG(SEV_INFO, "USAGE", "        -s      suppress output entirely (for debugging purposes)");
    SQL_MSG(SEV_INFO, "USAGE", "        -T      (optional) Truncate silence - don't output any samples when squelched"); /* thanks K9ETS for the name idea */
    exit(EXIT_SUCCESS);
}

/**
 * @brief Sets options from command line arguments
 * 
 * @param argc number of command line arguments
 * @param argv array of arguments
 */
static void _set_options(int argc, char * const argv[])
{
    int arg = -1;
    const char *out_fifo_path = NULL;

    while ((arg = getopt(argc, argv, "S:M:L:o:TshP")) != -1) {
        switch (arg) {
        case 'S':
            samplerate = strtoll(optarg, NULL, 0);
            break;
        case 'M':
            sql_mode = strtoll(optarg, NULL, 0);
            break;
        case 'L':
            sql_level = strtoll(optarg, NULL, 0);
            break;
        case 'o':
            out_fifo_path = optarg;
            break;
        case 'T':
            truncate_silence = 1;
            break;
        case 's':
            suppress_output = 1;
            break;
        case 'P':
            print_debug = 1;
            break;
        case 'h':
            _usage(argv[0]);
            break;
        }
    }

    /* Verify we have an input fifo specified */
    if (optind > argc) {
        SQL_MSG(SEV_FATAL, "MISSING-SRC-DEST", "Missing input fifo");
        exit(EXIT_FAILURE);
    }
    /* Verify min sample rate (necessary for our CSQ noise filtering) */
    if (!samplerate) {
        SQL_MSG(SEV_FATAL, "NO-SAMPLERATE", "Missing samplerate parameter -S");
        exit(EXIT_FAILURE);
    }
    /* Verify squelch mode */
    if (sql_mode != 0) {
        SQL_MSG(SEV_FATAL, "BAD-SQL-MODE", "Invalid squelch mode specified: %u", sql_mode);
        exit(EXIT_FAILURE);
    }
    /* Verify squelch level */
    if (sql_level > 10 || sql_level < 0) {
        SQL_MSG(SEV_FATAL, "BAD-SQL-LEVEL", "Invalid squelch level specified: %u", sql_level);
        exit(EXIT_FAILURE);
    }

    /* Init FIR filter based on specified sample rate */
    SQL_MSG(SEV_INFO, "FILTER-COEFFS", "Loading filter coefficients");
    double *filter_coeffs_f;
    switch (samplerate) {
        case 12000:
            filter_coeffs_f = hpf_coeffs_12k;
            nr_filter_coeffs = sizeof(hpf_coeffs_12k)/sizeof(hpf_coeffs_12k[0]);
            break;
        case 16000:
            filter_coeffs_f = hpf_coeffs_16k;
            nr_filter_coeffs = sizeof(hpf_coeffs_16k)/sizeof(hpf_coeffs_16k[0]);
            break;
        case 20000:
            filter_coeffs_f = hpf_coeffs_20k;
            nr_filter_coeffs = sizeof(hpf_coeffs_20k)/sizeof(hpf_coeffs_20k[0]);
            break;
        case 24000:
            filter_coeffs_f = hpf_coeffs_24k;
            nr_filter_coeffs = sizeof(hpf_coeffs_24k)/sizeof(hpf_coeffs_24k[0]);
            break;
        default:
            SQL_MSG(SEV_FATAL, "FILTER-COEFFS", "Invalid sample rate specified: %d", samplerate);
            exit(EXIT_FAILURE);
    }

    /* Convert float FIR coeffs to int16 */
    SQL_MSG(SEV_INFO, "FILTER-COEFFS", "Loaded filter with %lu coefficients", nr_filter_coeffs);
    SQL_MSG(SEV_INFO, "FILTER-COEFFS", "Converting coefficients to fixed-point");
    TSL_BUG_IF_FAILED(TCALLOC((void **)&filter_coeffs, sizeof(int16_t) * nr_filter_coeffs, (size_t)1));
    for (size_t i = 0; i < nr_filter_coeffs; i++) {
        double q15 = 1 << Q_15_SHIFT;
        filter_coeffs[i] = (int16_t)(filter_coeffs_f[i] * q15);
    }

    /* Init FIR filter */
    polyphase_fir_new(&hpfir, nr_filter_coeffs, filter_coeffs, 1, 1);

    /* Verify & open input fifo if valid */
    if (0 > (in_fifo = open(argv[optind], O_RDONLY))) {
        SQL_MSG(SEV_FATAL, "INV-IN-FIFO", "Cannot open input FIFO: %s", argv[optind]);
        exit(EXIT_FAILURE);
    }
    /* Open our output fifo if specified */
    if (out_fifo_path) {
        if (0 > (out_fifo = open(out_fifo_path, O_WRONLY))) {
            SQL_MSG(SEV_FATAL, "INV-OUT-FIFO", "Cannot open output FIFO: %s", out_fifo_path);
            exit(EXIT_FAILURE);
        }
    }
}

/**
 * @brief Free up a sample buffer
 * 
 * @param buf 
 * @return aresult_t 
 */
static aresult_t _free_sample_buf(struct sample_buf *buf)
{
    TSL_BUG_ON(NULL == buf);
    TFREE(buf);
    return A_OK;
}

/**
 * @brief Allocate a sample buffer
 * 
 * @param pbuf 
 * @return aresult_t 
 */
static aresult_t _alloc_sample_buf(struct sample_buf **pbuf)
{
    aresult_t ret = A_OK;

    struct sample_buf *buf = NULL;

    TSL_ASSERT_ARG(NULL != pbuf);

    if (FAILED(ret = TCALLOC((void **)&buf, NR_SAMPLES * sizeof(int16_t) + sizeof(struct sample_buf), 1ul))) {
        goto done;
    }

    buf->refcount = 0;
    buf->sample_type = COMPLEX_INT_16;
    buf->sample_buf_bytes = NR_SAMPLES * sizeof(int16_t);
    buf->nr_samples = 0;
    buf->release = _free_sample_buf;
    buf->priv = NULL;

    *pbuf = buf;

done:
    return ret;
}

/* Buffer for filtered samples */
static int16_t filter_buf[NR_SAMPLES];

/* Array of zeroes for silence */
static int16_t zero_buf[NR_SAMPLES] = {0};

/* Running squelch average */
static int16_t squelch_avg;

/* Char (bool-ish) for squelch state */
static char squelched = 0;

/* Processing Loop */
static aresult_t process_sql(void)
{
    int ret = A_OK;

    do {
        int op_ret = 0;
        struct sample_buf *read_buf = NULL;
        size_t new_samples = 0;
        bool full = false;

        TSL_BUG_IF_FAILED(polyphase_fir_full(hpfir, &full));

        if (false == full) {
            TSL_BUG_IF_FAILED(_alloc_sample_buf(&read_buf));

            if (0 >= (op_ret = read(in_fifo, read_buf->data_buf, read_buf->sample_buf_bytes))) {
                int errnum = errno;
                ret = A_E_INVAL;
                SQL_MSG(SEV_FATAL, "READ-FIFO-FAIL", "Failed to read from input fifo: %s (%d)",
                        strerror(errnum), errnum);
                goto done;
            }

            DIAG("Read %d bytes from input FIFO", op_ret);

            TSL_BUG_ON((1 & op_ret) != 0);

            read_buf->nr_samples = op_ret/sizeof(int16_t);

            TSL_BUG_IF_FAILED(polyphase_fir_push_sample_buf(hpfir, read_buf));
        }

        //SQL_MSG(SEV_INFO, "INPUT", "Read %d bytes from input", op_ret);

        /* Filter the samples */
        TSL_BUG_IF_FAILED(polyphase_fir_process(hpfir, filter_buf, NR_SAMPLES, &new_samples));
        TSL_BUG_ON(0 == new_samples);

        /* Don't run squelch routine if CSQ is set to zero */
        if (!sql_level) {
            squelched = 0;
        } else {
            // Calculate power of filtered samples and add to running average 
            squelch_avg = (uint16_t)((1 - SQL_SMOOTH) * squelch_avg) + (uint16_t)(get_pow(filter_buf, NR_SAMPLES) * SQL_SMOOTH / 125);
            if (squelch_avg > SQL_MAX) {squelch_avg = SQL_MAX;}
            if (print_debug) {
                SQL_MSG(SEV_INFO, "SQL_CALC", "Squelch average: %u", squelch_avg);
            }
            // Squelch or unsquelch the audio 
            if (squelch_avg >= (10-sql_level)) {
                squelched = 1;
            } else {
                if (print_debug) {
                    SQL_MSG(SEV_INFO, "SQL", "Unsquelched");
                }
                squelched = 0;
            }
        }

        /* Don't output anything unless suppression is disabled */
        if (suppress_output == 0) {
            if (squelched) {
                /* If we're truncating silence, do nothing, otherwise output silence */
                if (!truncate_silence) {
                    /* Write to stdout */
                    if (out_fifo < 0) {
                        if (0 > (op_ret = write(STDOUT_FILENO, zero_buf, read_buf->nr_samples * sizeof(int16_t)))) {
                            int errnum = errno;
                            ret = A_E_INVAL;
                            SQL_MSG(SEV_FATAL, "WRITE-STDOUT-FAIL", "Failed to write to stdout: %s (%d)",
                                    strerror(errnum), errnum);
                            goto done;
                        }
                    /* Write to output fifo */
                    } else {
                        if (0 > (op_ret = write(out_fifo, zero_buf, read_buf->nr_samples * sizeof(int16_t)))) {
                            int errnum = errno;
                            ret = A_E_INVAL;
                            SQL_MSG(SEV_FATAL, "WRITE-FIFO-FAIL", "Failed to write to output fifo: %s (%d)",
                                    strerror(errnum), errnum);
                            goto done;
                        }
                        DIAG("Wrote %d bytes to output FIFO", op_ret);
                    }
                }
            } else {
                /* Write the read samples back to the output */
                if (out_fifo < 0) {
                    /* Write to stdout */
                    if (0 > (op_ret = write(STDOUT_FILENO, read_buf->data_buf, read_buf->nr_samples * sizeof(int16_t)))) {
                        int errnum = errno;
                        ret = A_E_INVAL;
                        SQL_MSG(SEV_FATAL, "WRITE-STDOUT-FAIL", "Failed to write to stdout: %s (%d)",
                                strerror(errnum), errnum);
                        goto done;
                    }
                } else {
                    /* Write to output fifo */
                    if (0 > (op_ret = write(out_fifo, read_buf->data_buf, read_buf->nr_samples * sizeof(int16_t)))) {
                        int errnum = errno;
                        ret = A_E_INVAL;
                        SQL_MSG(SEV_FATAL, "WRITE-FIFO-FAIL", "Failed to write to output fifo: %s (%d)",
                                strerror(errnum), errnum);
                        goto done;
                    }
                    DIAG("Wrote %d bytes to output FIFO", op_ret);
                }
            }
        }

        //SQL_MSG(SEV_INFO, "OUTPUT", "Wrote %lu bytes to output", read_buf->nr_samples * sizeof(int16_t));

    } while (app_running());

    done:
        return ret;
}

/**
 * @brief Main runtime for squelch runtime
 * 
 * @param argc number of arguments (input fifo, )
 * @param argv arguments passed
 * @return int 
 */
int main(int argc, char *argv[])
{
    /* Starting return code */
    int ret = EXIT_FAILURE;

    TSL_BUG_IF_FAILED(app_init("squelch", NULL));
    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));
    
    /* Get Arguments */
    _set_options(argc, argv);

    /* Print Arguments */
    SQL_MSG(SEV_INFO, "SETUP", "Configured squelch parameters:");
    SQL_MSG(SEV_INFO, "SETUP", "    - Samplerate:    %u", samplerate);
    SQL_MSG(SEV_INFO, "SETUP", "    - Squelch Mode:  %i (%s)", sql_mode, _mode_name(sql_mode));
    SQL_MSG(SEV_INFO, "SETUP", "    - Squelch Level: %u", sql_level);
    if (truncate_silence) { SQL_MSG(SEV_INFO, "SETUP", "    - Truncated silence"); }
    if (suppress_output) { SQL_MSG(SEV_INFO, "SETUP", "    - Suppressing output samples"); }

    SQL_MSG(SEV_INFO, "MAIN", "Starting sample processing");

    if (FAILED(process_sql())) {
        SQL_MSG(SEV_FATAL, "SQL-FAILED", "Failed during squelch processing");
        goto done;
    }

    done:
    return ret;
}