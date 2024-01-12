/*
 *  decoder.c - Demodulate and decode various FSK transmissions
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
#include <pager/pager_pocsag.h>

#include <ais/ais_decode.h>

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
#include <ctype.h>
#include <time.h>
#include <inttypes.h>

#define DEC_MSG(sev, sys, msg, ...) MESSAGE("DECODER", sev, sys, msg, ##__VA_ARGS__)

enum decoder_decoder_type {
    DECODER_PAGER_TYPE_FLEX = 0,
    DECODER_PAGER_TYPE_POCSAG = 1,
    DECODER_PROTO_TYPE_AIS = 2,
};

static
enum decoder_decoder_type _decoder_type = DECODER_PAGER_TYPE_FLEX;

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
unsigned center_freq = 0;

static
struct pager_flex *flex = NULL;

static
struct pager_pocsag *pocsag = NULL;

static
struct ais_decode *ais_decode = NULL;

static
int sample_debug_fd = -1;

static
double dc_block_pole = 0.9999;

static
bool _invert = false;

static
void _usage(const char *appname)
{
    DEC_MSG(SEV_INFO, "USAGE", "%s -I [interpolate] -D [decimate] -F [filter file] -d [sample_debug_file] -S [input sample rate] -f [center freq] [-c] [-o output JSON file] [-b] [-i] [in_fifo]",
            appname);
    DEC_MSG(SEV_INFO, "USAGE", "        -b        Enable DC blocking filter          ");
    DEC_MSG(SEV_INFO, "USAGE", "        -c        Create JSON output file            ");
    DEC_MSG(SEV_INFO, "USAGE", "        -i        Invert input sample stream         ");
    DEC_MSG(SEV_INFO, "USAGE", "        -m [type] Specify protocol to decode         ");
    DEC_MSG(SEV_INFO, "USAGE", "           POCSAG - the POCSAG pager protocol        ");
    DEC_MSG(SEV_INFO, "USAGE", "           FLEX   - Motorola FLEX pager protocol     ");
    DEC_MSG(SEV_INFO, "USAGE", "           AIS    - Automatic Identification System  ");
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
FILE *out_file = NULL;

static inline
void _decoder_put_alnum_char(FILE *fp, char ch)
{
    switch (ch) {
    case '\n':
        fprintf(fp, "\\n");
        break;
    case '\r':
        fprintf(fp, "\\n");
        break;
    case '\"':
        fprintf(fp, "\\\"");
        break;
    case '\\':
        fprintf(fp, "\\\\");
        break;
    case '/':
        fprintf(fp, "\\/");
        break;
    case '\b':
        fprintf(fp, "<BKSP>");
        break;
    case '\f':
        fprintf(fp, "<FF>");
        break;
    case '\t':
        fprintf(fp, "\\t");
        break;
    case 0x03:
    case 0x04:
    case 0x17:
        fprintf(fp, " ");
        break;
    default:
        if (isprint(ch)) {
            fprintf(fp, "%c", ch);
        } else {
            fprintf(fp, "\\u%04x", (unsigned)ch);
        }
    }
}

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
        size_t message_len,
	uint32_t freq_hz)
{
    /* TODO: this sucks, should move it closer to the capture clock */
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    fprintf(out_file, "{\"proto\":\"flex\",\"type\":\"alphanumeric\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"baud\":%i,\"syncLevel\":%i,\"frameNo\":%u,\"cycleNo\":%u,\"phaseNo\":\"%c\",\"capCode\":%"PRIu64",\"fragment\":%s,"
            "\"maildrop\":%s,\"fragSeq\":%u,\"freq_hz\":%u,\"message\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            baud, 0, frame_no, cycle_no, phase_id[phase], cap_code,
            fragmented ? "true" : "false", maildrop ? "true" : "false", seq_num, freq_hz);

    for (size_t i = 0; i < message_len; i++) {
        _decoder_put_alnum_char(out_file, message_bytes[i]);
    }

    fprintf(out_file, "\"}\n");
    fflush(out_file);

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
        size_t message_len,
	uint32_t freq_hz)
{
    /* TODO: this sucks, should move it closer to the capture clock */
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    fprintf(out_file, "{\"proto\":\"flex\",\"type\":\"numeric\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"baud\":%i,\"syncLevel\":%i,\"frameNo\":%u,\"cycleNo\":%u,\"phaseNo\":\"%c\",\"capCode\":%"PRIu64","
	    "\"freq_hz\":%u,\"message\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            baud, 0, frame_no, cycle_no, phase_id[phase], cap_code, freq_hz);

    for (size_t i = 0; i < message_len; i++) {
        _decoder_put_alnum_char(out_file, message_bytes[i]);
    }

    fprintf(out_file, "\"}\n");
    fflush(out_file);

    return A_OK;
}

static
aresult_t _on_flex_siv_msg(
        struct pager_flex *f,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        uint8_t siv_msg_type,
        uint32_t data,
	uint32_t freq_hz)
{
    /* TODO: this sucks, should move it closer to the capture clock */
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    switch (siv_msg_type) {
    case PAGER_FLEX_SIV_TEMP_ADDRESS_ACTIVATION:
        fprintf(out_file, "{\"proto\":\"flex\",\"type\":\"tempAddrActivation\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
                "\"baud\":%i,\"syncLevel\":%i,\"frameNo\":%u,\"cycleNo\":%u,\"phaseNo\":\"%c\",\"capCode\":%"PRIu64",\"startFrameNo\":%u,\"tempAddressId\":%u,\"freq_hz\":%u}\n",
                gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
                baud, 0, frame_no, cycle_no, phase_id[phase], cap_code, data & 0x7f, (data >> 7) & 0xf, freq_hz);
        break;
    }
    return A_OK;
}

static
aresult_t _on_pocsag_alnum_msg(
        struct pager_pocsag *p,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len,
        uint8_t function,
	uint32_t freq_hz)
{
    /* TODO: this sucks, should move it closer to the capture clock */
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    fprintf(out_file, "{\"proto\":\"pocsag\",\"type\":\"alphanumeric\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"baud\":%i,\"capCode\":%u,\"function\":%u,\"freq_hz\":%u,\"message\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            baud_rate, capcode, (unsigned)function, freq_hz);

    for (size_t i = 0; i < data_len; i++) {
        _decoder_put_alnum_char(out_file, data[i]);
    }

    fprintf(out_file, "\"}\n");
    fflush(out_file);

    return A_OK;
}

static
aresult_t _on_pocsag_num_msg(
        struct pager_pocsag *p,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len,
        uint8_t function,
	uint32_t freq_hz)
{
    /* TODO: this sucks, should move it closer to the capture clock */
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    fprintf(out_file, "{\"proto\":\"pocsag\",\"type\":\"numeric\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"baud\":%i,\"capCode\":%u,\"function\":%u,\"freq_hz\":%u,\"message\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            baud_rate, capcode, (unsigned)function, freq_hz);

    for (size_t i = 0; i < data_len; i++) {
        _decoder_put_alnum_char(out_file, data[i]);
    }

    fprintf(out_file, "\"}\n");
    fflush(out_file);

    return A_OK;
}

static
aresult_t _on_ais_position_report(struct ais_decode *decode, void *state, struct ais_position_report *pr, const char *raw_msg)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    fprintf(out_file,
            "{\"proto\":\"ais\",\"type\":\"positionReport\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"mmsi\":%u,\"navStat\":%u,\"rateOfTurn\":%d,\"speedOverGround\":%f,\"positionAcc\":%u,"
            "\"geoPosition\":{\"lon\":%f,\"lat\":%f},\"course\":%u,\"heading\":%u,\"seconds\":%u,\"rawAscii\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            pr->mmsi, pr->nav_stat, pr->rate_of_turn, (double)pr->speed_over_ground, pr->position_acc,
            (double)pr->longitude, (double)pr->latitude, pr->course, pr->heading, pr->timestamp);

    for (size_t i = 0; i < strlen(raw_msg); i++) {
        _decoder_put_alnum_char(out_file, raw_msg[i]);
    }

    fprintf(out_file, "\"}\n");

    return A_OK;
}

static
aresult_t _on_ais_base_station_report(struct ais_decode *decode, void *state, struct ais_base_station_report *br,
        const char *raw_msg)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    fprintf(out_file,
            "{\"proto\":\"ais\",\"type\":\"baseStationReport\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"mmsi\":%u,\"baseStationDate\":\"%04u-%02u-%02u %02u:%02u:%02u UTC\","
            "\"geoPosition\":{\"lon\":%f,\"lat\":%f},\"fixType\":\"%s\",\"rawAscii\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            br->mmsi, br->year, br->month, br->day, br->hour, br->minute, br->second,
            (double)br->longitude, (double)br->latitude, br->epfd_name);

    for (size_t i = 0; i < strlen(raw_msg); i++) {
        _decoder_put_alnum_char(out_file, raw_msg[i]);
    }

    fprintf(out_file, "\"}\n");

    return A_OK;
}

static
aresult_t _on_ais_static_voyage_data(struct ais_decode *decode, void *state, struct ais_static_voyage_data *svd,
        const char *raw_msg)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);

    /* TODO: Ensure we escape the callsign, ship name and destination */

    fprintf(out_file,
            "{\"proto\":\"ais\",\"type\":\"staticAndVoyageData\",\"timestamp\":\"%04i-%02i-%02i %02i:%02i:%02i UTC\","
            "\"mmsi\":%u,\"version\":%u,\"imoNumber\":%u,\"callsign\":\"%s\",\"shipName\":\"%s\","
            "\"shipType\":%u,\"dimensions\":{\"toBow\":%u,\"toStern\":%u,\"toPort\":%u,\"toStarboard\":%u},"
            "\"fixType\":\"%s\",\"eta\":\"%02u-%02u %02u:%02u\",\"draught\":%f,\"destination\":\"%s\","
            "\"rawAscii\":\"",
            gmt->tm_year + 1900, gmt->tm_mon + 1, gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec,
            svd->mmsi, svd->version, svd->imo_number, svd->callsign, svd->ship_name,
            svd->ship_type, svd->dim_to_bow, svd->dim_to_stern, svd->dim_to_port, svd->dim_to_starboard,
            svd->epfd_name, svd->eta_month, svd->eta_day, svd->eta_hour, svd->eta_minute, svd->draught, svd->destination);

    for (size_t i = 0; i < strlen(raw_msg); i++) {
        _decoder_put_alnum_char(out_file, raw_msg[i]);
    }

    fprintf(out_file, "\"}\n");

    return A_OK;
}

static
void _set_options(int argc, char * const argv[])
{
    int arg = -1;
    const char *filter_file = NULL,
               *out_file_name = NULL;
    struct config *cfg CAL_CLEANUP(config_delete) = NULL;
    double *filter_coeffs_f = NULL;
    bool create_out = false;

    while ((arg = getopt(argc, argv, "co:I:D:S:F:f:d:p:m:bih")) != -1) {
        switch (arg) {
        case 'o':
            out_file_name = optarg;
            break;
        case 'c':
            create_out = true;
            break;
        case 'f':
            center_freq = strtoll(optarg, NULL, 0);
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
            DEC_MSG(SEV_INFO, "DC-BLOCKER-ENABLED", "Enabling DC Blocking Filter.");
            break;

        case 'd':
            if (0 > (sample_debug_fd = open(optarg, O_WRONLY | O_CREAT, 0666))) {
                int errnum = errno;
                DEC_MSG(SEV_ERROR, "FAIL-DEBUG-FILE", "Failed to open debug output file %s: %s (%d)",
                        optarg, strerror(errnum), errnum);
                exit(EXIT_FAILURE);
            }
            break;

        case 'm':
            if (!strncasecmp(optarg, "pocsag", 6)) {
                _decoder_type = DECODER_PAGER_TYPE_POCSAG;
            } else if (!strncasecmp(optarg, "flex", 4)) {
                _decoder_type = DECODER_PAGER_TYPE_FLEX;
            } else if (!strncasecmp(optarg, "ais", 3)) {
                _decoder_type = DECODER_PROTO_TYPE_AIS;
            } else {
                DEC_MSG(SEV_ERROR, "UNKNOWN-PROTOCOL-TYPE", "Unknown protocol type specified: %s", optarg);
                exit(EXIT_FAILURE);
            }
            break;

        case 'p':
            dc_block_pole = strtod(optarg, NULL);
            DEC_MSG(SEV_INFO, "DC-BLOCK-POLE", "Setting DC Blocker pole to %f", dc_block_pole);
            break;

        case 'i':
            _invert = true;
            DEC_MSG(SEV_INFO, "INVERTING", "Inverting input sample stream, due to a non-phase correcting input source.");
            break;

        case 'h':
            _usage(argv[0]);
            break;
        }
    }

    if (optind > argc) {
        DEC_MSG(SEV_FATAL, "MISSING-SRC-DEST", "Missing source/destination file");
        exit(EXIT_FAILURE);
    }

    if (0 == decimate) {
        DEC_MSG(SEV_FATAL, "BAD-DECIMATION", "Decimation factor must be a non-zero integer.");
        exit(EXIT_FAILURE);
    }

    if (0 == decimate) {
        DEC_MSG(SEV_FATAL, "BAD-INTERPOLATION", "Interpolation factor must be a non-zero integer.");
        exit(EXIT_FAILURE);
    }

    if (0 == center_freq) {
        DEC_MSG(SEV_FATAL, "BAD-PAGER-FREQ", "Pager frequency must be non-zero");
        exit(EXIT_FAILURE);
    }

    if (NULL == filter_file) {
        DEC_MSG(SEV_FATAL, "BAD-FILTER-FILE", "Need to specify a filter JSON file.");
        exit(EXIT_FAILURE);
    }

    if (NULL == out_file_name) {
        DEC_MSG(SEV_INFO, "WRITE-TO-STDOUT", "Output decoded data is going to stdout.");
        out_file = stdout;
    } else {
        if (create_out) {
            DEC_MSG(SEV_INFO, "CREATING-OUTPUT", "Creating output file '%s', will overwrite if it exists",
                    out_file_name);
        } else {
            DEC_MSG(SEV_INFO, "OPENING-OUTPUT", "Opening output file '%s', will append to end if it exists",
                    out_file_name);
        }
        if (NULL == (out_file = fopen(out_file_name, create_out ? "w+" : "a"))) {
            DEC_MSG(SEV_INFO, "BAD-OUTPUT-FILE", "Failed to open output file '%s', aborting.",
                    out_file_name);
            exit(EXIT_FAILURE);
        }
    }

    DEC_MSG(SEV_INFO, "CONFIG", "Resampling: %u/%u from %u to %f", interpolate, decimate, input_sample_rate,
            ((double)interpolate/(double)decimate)*(double)input_sample_rate);
    DEC_MSG(SEV_INFO, "CONFIG", "Loading filter coefficients from '%s'", filter_file);

    TSL_BUG_IF_FAILED(config_new(&cfg));

    if (FAILED(config_add(cfg, filter_file))) {
        DEC_MSG(SEV_INFO, "BAD-CONFIG", "Configuration file '%s' cannot be processed, aborting.",
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
        DEC_MSG(SEV_INFO, "BAD-INPUT", "Bad input - cannot open %s", argv[optind]);
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

    buf->refcount = 1;
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
    struct sample_buf *read_buf = NULL;
    size_t sample_count = 0;

    TSL_BUG_IF_FAILED(dc_blocker_init(&blck, dc_block_pole));

    do {
        int op_ret = 0;
        size_t new_samples = 0;
        bool full = false;

        TSL_BUG_IF_FAILED(polyphase_fir_full(pfir, &full));

        if (false == full) {
            size_t nr_sample_bytes = 0;

            if (NULL == read_buf) {
                /* Allocate a new buffer */
                TSL_BUG_IF_FAILED(_alloc_sample_buf(&read_buf));
            }

            nr_sample_bytes = read_buf->nr_samples * sizeof(int16_t);

            if (0 >= (op_ret = read(in_fifo, (uint8_t *)read_buf->data_buf + nr_sample_bytes, read_buf->sample_buf_bytes - nr_sample_bytes))) {
                int errnum = errno;
                ret = A_E_INVAL;
                DEC_MSG(SEV_FATAL, "READ-FIFO-FAIL", "Failed to read from input fifo: %s (%d)",
                        strerror(errnum), errnum);
                goto done;
            }

            TSL_BUG_ON((1 & op_ret) != 0);

            read_buf->nr_samples += op_ret/sizeof(int16_t);
            sample_count += op_ret/sizeof(int16_t);

            if (true == _invert) {
                int16_t *samp = (int16_t *)read_buf->data_buf;
                for (size_t i = 0; i < read_buf->nr_samples; i++) {
                    samp[i] *= -1;
                }
            }

            if (read_buf->nr_samples == NR_SAMPLES) {
                TSL_BUG_IF_FAILED(polyphase_fir_push_sample_buf(pfir, read_buf));
                read_buf = NULL;
            }
        }

        /* Filter the samples, decimating as appropriate */
        TSL_BUG_IF_FAILED(polyphase_fir_process(pfir, output_buf, NR_SAMPLES, &new_samples));

        if (0 == new_samples) {
            /* Skip further sample processing */
            continue;
        }

        /* Apply DC blocker, if asked */
        if (true == dc_blocker) {
            TSL_BUG_IF_FAILED(dc_blocker_apply(&blck, output_buf, new_samples));
        }

        /* Process with the protocol object */
        if (_decoder_type == DECODER_PAGER_TYPE_FLEX) {
            TSL_BUG_IF_FAILED(pager_flex_on_pcm(flex, output_buf, new_samples));
        } else if (_decoder_type == DECODER_PAGER_TYPE_POCSAG) {
            TSL_BUG_IF_FAILED(pager_pocsag_on_pcm(pocsag, output_buf, new_samples));
        } else if (_decoder_type == DECODER_PROTO_TYPE_AIS) {
            TSL_BUG_IF_FAILED(ais_decode_on_pcm(ais_decode, output_buf, new_samples));
        } else {
            PANIC("Unknown decoder type, aborting");
        }

        /* If a sample debug file was specified, write to the sample debug file */
        if (-1 != sample_debug_fd) {
            if (0 > write(sample_debug_fd, output_buf, new_samples * sizeof(int16_t))) {
                int errnum = errno;
                DEC_MSG(SEV_FATAL, "WRITE-DEBUG-FAIL", "Failed to write to output debug file: %s (%d)",
                        strerror(errnum), errnum);
            }
        }

        /* Release the sample buffer */
    } while (app_running());

done:
    DEC_MSG(SEV_INFO, "TERMINATING", "Terminating processing loop, processed %zu samples", sample_count);
    return ret;
}

int main(int argc, char * const argv[])
{
    int ret = EXIT_FAILURE;

    TSL_BUG_IF_FAILED(app_init("resampler", NULL));
    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));

    _set_options(argc, argv);

    /* Create the polyphase resampling filter */
    TSL_BUG_IF_FAILED(polyphase_fir_new(&pfir, nr_filter_coeffs, filter_coeffs, interpolate, decimate));

    /* Set up the appropriate protocol decoder */
    if (_decoder_type == DECODER_PAGER_TYPE_FLEX) {
        DEC_MSG(SEV_INFO, "PROTOCOL", "Using the Motorola FLEX pager protocol.");
        TSL_BUG_IF_FAILED(pager_flex_new(&flex, center_freq, _on_flex_alnum_msg, _on_flex_num_msg, _on_flex_siv_msg));
    } else if (_decoder_type == DECODER_PAGER_TYPE_POCSAG) {
        DEC_MSG(SEV_INFO, "PROTOCOL", "Using the POCSAG Pager Protocol.");
        TSL_BUG_IF_FAILED(pager_pocsag_new(&pocsag, center_freq, _on_pocsag_num_msg, _on_pocsag_alnum_msg, false));
    } else if (_decoder_type == DECODER_PROTO_TYPE_AIS) {
        DEC_MSG(SEV_INFO, "PROTOCOL", "Using the AIS Message Format.");
        TSL_BUG_IF_FAILED(ais_decode_new(&ais_decode, center_freq, _on_ais_position_report, _on_ais_base_station_report, _on_ais_static_voyage_data));
    }

    DEC_MSG(SEV_INFO, "STARTING", "Starting message decoder on frequency %u Hz.", center_freq);

    if (FAILED(process_samples())) {
        DEC_MSG(SEV_FATAL, "FIR-FAILED", "Failed during message processing, aborting.");
        goto done;
    }

    ret = EXIT_SUCCESS;

done:
    if (NULL != out_file && stdout != out_file) {
        fclose(out_file);
    }

    if (NULL != flex) {
        pager_flex_delete(&flex);
    }

    if (NULL != pocsag) {
        pager_pocsag_delete(&pocsag);
    }

    if (NULL != ais_decode) {
        ais_decode_delete(&ais_decode);
    }

    if (NULL != pfir) {
        polyphase_fir_delete(&pfir);
    }

    return ret;
}

