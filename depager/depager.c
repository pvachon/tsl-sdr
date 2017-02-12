/*
 *  depager.c - Demodulate and decode FLEX pager transmissions
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
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
#include <pager/pager_flex.h>

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

#define DEP_MSG(sev, sys, msg, ...) MESSAGE("DEPAGER", sev, sys, msg, ##__VA_ARGS__)

static
unsigned interpolate = 1;

static
unsigned decimate = 1;

static
unsigned input_sample_rate = 0;

static
int in_fifo = -1;

static
int16_t *filter_coeffs = NULL;

static
size_t nr_filter_coeffs = 0;

static
struct polyphase_fir *pfir = NULL;

static
bool dc_blocker = false;

static
unsigned pager_freq = 0;

static
struct pager_flex *flex = NULL;

static
int sample_debug_fd = -1;

static
double dc_block_pole = 0.9999;

static
void _usage(const char *appname)
{
    DEP_MSG(SEV_INFO, "USAGE", "%s -I [interpolate] -D [decimate] -F [filter file] -d [sample_debug_file] -S [sample rate] -f [pager chan freq] [-b] [in_fifo]",
            appname);
    DEP_MSG(SEV_INFO, "USAGE", "        -b      Enable DC blocking filter");
    exit(EXIT_SUCCESS);
}

static const
char phase_id[] = {
    [0] = 'A',
    [1] = 'B',
    [2] = 'C',
    [3] = 'D',
};

static
aresult_t _on_flex_alnum_msg(
        struct pager_flex *f,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        bool fragmented,
        bool maildrop,
        uint8_t seq_num,
        const char *message_bytes,
        size_t message_len)
{
    printf("[ALN] CAPCODE: %9zu | %4u/%c (%2u:%2u) | %c%c [%1u] | ",
            cap_code, baud, phase_id[phase], cycle_no, frame_no, fragmented ? 'F' : '-',
            maildrop ? 'M' : '-', seq_num);
    for (size_t i = 0; i < message_len; i++) {
        printf("%c", message_bytes[i]);
    }
    printf("\n");
    fflush(stdout);
    return A_OK;
}

static
aresult_t _on_flex_num_msg(
        struct pager_flex *f,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        const char *message_bytes,
        size_t message_len)
{
    printf("[NUM] CAPCODE: %9zu | %4u/%c (%2u:%2u) |        | ",
            cap_code, baud, phase_id[phase], cycle_no, frame_no);
    for (size_t i = 0; i < message_len; i++) {
        printf("%c", message_bytes[i]);
    }
    printf("\n");
    fflush(stdout);
    return A_OK;
}

static
void _set_options(int argc, char * const argv[])
{
    int arg = -1;
    const char *filter_file = NULL;
    struct config *cfg CAL_CLEANUP(config_delete) = NULL;
    double *filter_coeffs_f = NULL;

    while ((arg = getopt(argc, argv, "I:D:S:F:f:d:p:bh")) != -1) {
        switch (arg) {
        case 'f':
            pager_freq = strtoll(optarg, NULL, 0);
            break;
        case 'I':
            interpolate = strtoll(optarg, NULL, 0);
            break;
        case 'D':
            decimate = strtoll(optarg, NULL, 0);
            break;
        case 'S':
            input_sample_rate = strtoll(optarg, NULL, 0);
            break;
        case 'F':
            filter_file = optarg;
            break;
        case 'b':
            dc_blocker = true;
            DEP_MSG(SEV_INFO, "DC-BLOCKER-ENABLED", "Enabling DC Blocking Filter.");
            break;

        case 'd':
            if (0 > (sample_debug_fd = open(optarg, O_WRONLY | O_CREAT, 0666))) {
                int errnum = errno;
                DEP_MSG(SEV_ERROR, "FAIL-DEBUG-FILE", "Failed to open debug output file %s: %s (%d)",
                        optarg, strerror(errnum), errnum);
                exit(EXIT_FAILURE);
            }
            break;

        case 'p':
            dc_block_pole = strtod(optarg, NULL);
            DEP_MSG(SEV_INFO, "DC-BLOCK-POLE", "Setting DC Blocker pole to %f", dc_block_pole);
            break;

        case 'h':
            _usage(argv[0]);
            break;
        }
    }

    if (optind > argc) {
        DEP_MSG(SEV_FATAL, "MISSING-SRC-DEST", "Missing source/destination file");
        exit(EXIT_FAILURE);
    }

    if (0 == decimate) {
        DEP_MSG(SEV_FATAL, "BAD-DECIMATION", "Decimation factor must be a non-zero integer.");
        exit(EXIT_FAILURE);
    }

    if (0 == decimate) {
        DEP_MSG(SEV_FATAL, "BAD-INTERPOLATION", "Interpolation factor must be a non-zero integer.");
        exit(EXIT_FAILURE);
    }

    if (0 == pager_freq) {
        DEP_MSG(SEV_FATAL, "BAD-PAGER-FREQ", "Pager frequency must be non-zero");
        exit(EXIT_FAILURE);
    }

    if (NULL == filter_file) {
        DEP_MSG(SEV_FATAL, "BAD-FILTER-FILE", "Need to specify a filter JSON file.");
        exit(EXIT_FAILURE);
    }

    DEP_MSG(SEV_INFO, "CONFIG", "Resampling: %u/%u from %u to %f", interpolate, decimate, input_sample_rate,
            ((double)interpolate/(double)decimate)*(double)input_sample_rate);
    DEP_MSG(SEV_INFO, "CONFIG", "Loading filter coefficients from '%s'", filter_file);

    TSL_BUG_IF_FAILED(config_new(&cfg));

    if (FAILED(config_add(cfg, filter_file))) {
        DEP_MSG(SEV_INFO, "BAD-CONFIG", "Configuration file '%s' cannot be processed, aborting.",
                filter_file);
        exit(EXIT_FAILURE);
    }

    TSL_BUG_IF_FAILED(config_get_float_array(cfg, &filter_coeffs_f, &nr_filter_coeffs, "lpfCoeffs"));
    TSL_BUG_IF_FAILED(TCALLOC((void **)&filter_coeffs, sizeof(int16_t) * nr_filter_coeffs, (size_t)1));

    for (size_t i = 0; i < nr_filter_coeffs; i++) {
        double q15 = 1 << Q_15_SHIFT;
        filter_coeffs[i] = (int16_t)(filter_coeffs_f[i] * q15);
    }

    if (0 > (in_fifo = open(argv[optind], O_RDONLY))) {
        DEP_MSG(SEV_INFO, "BAD-INPUT", "Bad input - cannot open %s", argv[optind]);
        exit(EXIT_FAILURE);
    }
}

static
aresult_t _free_sample_buf(struct sample_buf *buf)
{
    TSL_BUG_ON(NULL == buf);
    TFREE(buf);
    return A_OK;
}

#define NR_SAMPLES                  1024

static
aresult_t _alloc_sample_buf(struct sample_buf **pbuf)
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

static
int16_t output_buf[NR_SAMPLES];

static
aresult_t process_samples(void)
{
    int ret = A_OK;

    struct dc_blocker blck;

    TSL_BUG_IF_FAILED(dc_blocker_init(&blck, dc_block_pole));

    do {
        int op_ret = 0;
        struct sample_buf *read_buf = NULL;
        size_t new_samples = 0;
        bool full = false;

        TSL_BUG_IF_FAILED(polyphase_fir_full(pfir, &full));

        if (false == full) {
            TSL_BUG_IF_FAILED(_alloc_sample_buf(&read_buf));

            if (0 >= (op_ret = read(in_fifo, read_buf->data_buf, read_buf->sample_buf_bytes))) {
                int errnum = errno;
                ret = A_E_INVAL;
                DEP_MSG(SEV_FATAL, "READ-FIFO-FAIL", "Failed to read from input fifo: %s (%d)",
                        strerror(errnum), errnum);
                goto done;
            }

            //DIAG("Read %d bytes from input FIFO", op_ret);

            TSL_BUG_ON((1 & op_ret) != 0);

            read_buf->nr_samples = op_ret/sizeof(int16_t);

            TSL_BUG_IF_FAILED(polyphase_fir_push_sample_buf(pfir, read_buf));
        }

        /* Filter the samples, decimating as appropriate */
        TSL_BUG_IF_FAILED(polyphase_fir_process(pfir, output_buf, NR_SAMPLES, &new_samples));
        TSL_BUG_ON(0 == new_samples);

        /* Apply DC blocker, if asked */
        if (true == dc_blocker) {
            TSL_BUG_IF_FAILED(dc_blocker_apply(&blck, output_buf, new_samples));
        }

        /* Process with the pager object */
        TSL_BUG_IF_FAILED(pager_flex_on_pcm(flex, output_buf, new_samples));

        /* If a sample debug file was specified, write to the sample debug file */
        if (-1 != sample_debug_fd) {
            if (0 > write(sample_debug_fd, output_buf, new_samples * sizeof(int16_t))) {
                int errnum = errno;
                DEP_MSG(SEV_FATAL, "WRITE-DEBUG-FAIL", "Failed to write to output debug file: %s (%d)",
                        strerror(errnum), errnum);
            }
        }

    } while (app_running());

done:
    return ret;
}

int main(int argc, char * const argv[])
{
    int ret = EXIT_FAILURE;

    TSL_BUG_IF_FAILED(app_init("resampler", NULL));
    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));

    _set_options(argc, argv);
    TSL_BUG_IF_FAILED(polyphase_fir_new(&pfir, nr_filter_coeffs, filter_coeffs, interpolate, decimate));
    TSL_BUG_IF_FAILED(pager_flex_new(&flex, pager_freq, _on_flex_alnum_msg, _on_flex_num_msg));

    DEP_MSG(SEV_INFO, "STARTING", "Starting pager message decoder on frequency %u Hz.", pager_freq);

    if (FAILED(process_samples())) {
        DEP_MSG(SEV_FATAL, "FIR-FAILED", "Failed during pager processing, aborting.");
        goto done;
    }

    ret = EXIT_SUCCESS;

done:
    pager_flex_delete(&flex);
    polyphase_fir_delete(&pfir);
    return ret;
}

