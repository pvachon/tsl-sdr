#include <dect/dect.h>

#include <app/app.h>

#include <tsl/safe_alloc.h>
#include <tsl/safe_string.h>
#include <tsl/result.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>

static
char *_dect_in_fifo = NULL;

static
bool _dect_hamming_dist(uint32_t a, uint32_t b, size_t n)
{
    return (__builtin_popcountll(a ^ b) < n);
}

static
uint16_t _dect_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;

    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (size_t j = 0; j < 8; j++) {
            if ((crc & 0x8000) == 0x8000) {
                crc = (crc << 1) ^ 0x0589;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc ^ 0x1;
}

static
aresult_t dect_process_frame(struct dect_channel *chan)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != chan);

    /* Grab the A-field header */

    /* Check the A-field tail */

    return ret;
}

static
aresult_t dect_process_buf(struct dect_channel *chan, const uint8_t *buf, size_t buf_len)
{
    aresult_t ret = A_OK;

    size_t buf_cur_byte = 0,
           buf_cur_bit = 0;

    TSL_ASSERT_ARG(NULL != chan);
    TSL_ASSERT_ARG(NULL != buf);
    TSL_ASSERT_ARG(0 != buf_len);

    while (buf_len != buf_cur_byte) {
        for (size_t i = buf_cur_byte;
                i < buf_len &&
                    DECT_CHANNEL_FRAME_STATE_SYNC_SEARCH == chan->state;
                i++)
        {
            for (size_t j = buf_cur_bit; j < 8; j++) {
                chan->sync_word <<= 1;
                chan->sync_word |= (buf[i] >> (7 - j)) & 1;

                if (_dect_hamming_dist(chan->sync_word, DECT_FP_SYNC, 1) ||
                        _dect_hamming_dist(chan->sync_word, DECT_PP_SYNC, 1))
                {
                    buf_cur_bit = (j + 1) & 0x7;
                    chan->state = DECT_CHANNEL_FRAME_STATE_A_FIELD_WAIT;
                    DIAG("SYNC_SEARCH -> A_FIELD_WAIT (buf = %zu, j = %zu)", buf_cur_bit, j);
                    if (0 != buf_cur_bit) {
                        buf_cur_byte--;
                    }

                    chan->cur_bit = 0;
                    chan->cur_byte = 0;
                    chan->nr_bytes = 0;
                    chan->rem_bytes = DECT_FRAME_A_FIELD_LENGTH;

                    break;
                }
            }
            buf_cur_byte++;
        }

        while (buf_len > buf_cur_byte &&
                (DECT_CHANNEL_FRAME_STATE_A_FIELD_WAIT == chan->state ||
                 DECT_CHANNEL_FRAME_STATE_PROCESSING == chan->state))
        {
            while (chan->cur_bit < 8) {
                chan->cur_byte <<= 1;
                chan->cur_byte |= (buf[buf_cur_byte] >> (7 - buf_cur_bit)) & 1;
                chan->cur_bit++;
                buf_cur_bit = (buf_cur_bit + 1) & 0x7;

                if (0 == buf_cur_bit) {
                    buf_cur_byte++;
                    break;
                }
            }

            if (8 != chan->cur_bit) {
                /* Don't have a full byte accumulated here */
                continue;
            }

            chan->frame[chan->nr_bytes++] = chan->cur_byte;
            chan->cur_bit = 0;
            chan->cur_byte = 0;

            if (DECT_CHANNEL_FRAME_STATE_A_FIELD_WAIT == chan->state &&
                    DECT_FRAME_A_FIELD_LENGTH == chan->nr_bytes)
            {
                /* Process the A-field, figure out how long the B-field is */
                struct dect_frame_a_field *field = (void *)chan->frame;
                /* Check the CRC */
                uint16_t calc_crc = _dect_crc16(chan->frame, 6),
                         frame_crc = ((field->crc & 0xff) << 8 | field->crc >> 8);

                switch (DECT_FRAME_A_FIELD_HEADER_B_FIELD_TYPE(field->header)) {
                case DECT_HEADER_B_FIELD_NOT_PRESENT:
                    chan->b_frame_bytes = 0;
                    break;

                case DECT_HEADER_B_FIELD_HALF_SLOT:
                    chan->b_frame_bytes = DECT_HEADER_B_FIELD_LEN_HALF;
                    break;

                case DECT_HEADER_B_FIELD_DOUBLE_SLOT:
                    chan->b_frame_bytes = DECT_HEADER_B_FIELD_LEN_DOUBLE;
                    break;

                default:
                    chan->b_frame_bytes = DECT_HEADER_B_FIELD_LEN_REGULAR;
                }

                chan->rem_bytes = chan->b_frame_bytes;

                chan->state = DECT_CHANNEL_FRAME_STATE_PROCESSING;

                if (chan->rem_bytes != 0) {
                    fprintf(stderr, "Sync: %08x CRC [%s] Header: TailID: %2x B-Field: %2x CRC16: %04x CalcCRC16: %04x Len: %3zu [%02x - %02x %02x %02x %02x %02x - %02x %02x]\n",
                            (unsigned)chan->sync_word,
                            calc_crc == frame_crc ? "  OK  " : " FAIL ",
                            (unsigned)DECT_FRAME_A_FIELD_HEADER_TAIL_ID(field->header),
                            (unsigned)DECT_FRAME_A_FIELD_HEADER_B_FIELD_TYPE(field->header),
                            (unsigned)frame_crc,
                            (unsigned)calc_crc,
                            chan->rem_bytes,
                            chan->frame[0],
                            chan->frame[1],
                            chan->frame[2],
                            chan->frame[3],
                            chan->frame[4],
                            chan->frame[5],
                            chan->frame[6],
                            chan->frame[7]
                    );
                }

                if (calc_crc != frame_crc) {
                    chan->rem_bytes = 0;
                }

                DIAG("A_FIELD_WAIT -> PROCESSING");
                chan->state = DECT_CHANNEL_FRAME_STATE_PROCESSING;
            }

            if (chan->state == DECT_CHANNEL_FRAME_STATE_PROCESSING &&
                    chan->nr_bytes == chan->b_frame_bytes + DECT_FRAME_A_FIELD_LENGTH)
            {
                /* Dispatch processing for the captured frame */
                TSL_BUG_IF_FAILED(dect_process_frame(chan));

                /* Reset all the state */
                chan->state = DECT_CHANNEL_FRAME_STATE_SYNC_SEARCH;
                DIAG("PROCESSING -> SYNC_SEARCH");
                chan->nr_bytes = 0;
                chan->cur_bit = 0;
                chan->cur_byte = 0;
            }
        }
    }

    return ret;
}

static
aresult_t dect_channel_delete(struct dect_channel **pchannel)
{
    aresult_t ret = A_OK;

    struct dect_channel *channel = NULL;

    TSL_ASSERT_ARG(NULL != pchannel);
    TSL_ASSERT_ARG(NULL != *pchannel);

    channel = *pchannel;

    if (0 > channel->fd) {
        close(channel->fd);
        channel->fd = -1;
    }

    TFREE(channel);

    *pchannel = NULL;

    return ret;
}

static
aresult_t dect_channel_new(struct dect_channel **pchannel, const char *in_fifo)
{
    aresult_t ret = A_OK;

    struct dect_channel *chan = NULL;
    int chan_fd = -1;

    TSL_ASSERT_ARG(NULL != in_fifo);
    TSL_ASSERT_ARG(NULL != pchannel);

    if (0 > (chan_fd = open(in_fifo, O_RDONLY))) {
        DCT_MSG(SEV_FATAL, "CANT-OPEN-FIFO", "Unable to open file [%s], aborting",
                in_fifo);
        goto done;
    }

    if (FAILED(ret = TZAALLOC(chan, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    chan->fd = chan_fd;

    *pchannel = chan;

done:
    if (FAILED(ret)) {
        if (NULL != chan) {
            TFREE(chan);
        }

        if (-1 != chan_fd) {
            close(chan_fd);
            chan_fd = -1;
        }
    }
    return ret;
}

static
aresult_t _set_config(int argc, char * const *argv)
{
    aresult_t ret = A_OK;

    int opt = 0;

    TSL_ASSERT_ARG(0 != argc);
    TSL_ASSERT_ARG(NULL != argv);

    while (0 < (opt = getopt(argc, argv, "i:h"))) {
        switch (opt) {
        case 'i':
            TSL_BUG_IF_FAILED(tstrdup(&_dect_in_fifo, optarg));
            break;
        case 'h':
            DCT_MSG(SEV_INFO, "USAGE", "Usage: %s -i [pipe]", argv[0]);
            exit(EXIT_SUCCESS);
            break;
        }
    }

    return ret;
}

#define BUF_LEN             8192

int main(int argc, char * const *argv)
{
    int ret = EXIT_FAILURE;

    struct dect_channel *chan CAL_CLEANUP(dect_channel_delete) = NULL;
    void *buf = NULL;
    size_t buf_fill = 0;

    TSL_BUG_IF_FAILED(app_init("dect", NULL));
    TSL_BUG_IF_FAILED(_set_config(argc, argv));

    /* Create the DECT channel */
    TSL_BUG_IF_FAILED(dect_channel_new(&chan, _dect_in_fifo));

    TSL_BUG_IF_FAILED(TCALLOC((void **)&buf, BUF_LEN, 1));

    DIAG("Let's read this (fd = %d)!", chan->fd);

    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));

    /* Read the file */
    while (app_running()) {
        ssize_t bytes = 0;
        if (0 >= (bytes = read(chan->fd, buf, BUF_LEN - buf_fill))) {
            int errnum = errno;
            DCT_MSG(SEV_WARNING, "FAILED-TO-READ", "Unable to read more bytes "
                    "from file [%s] (got %zd, already have %zu)", _dect_in_fifo, bytes,
                    buf_fill);
            DCT_MSG(SEV_FATAL, "FILE-ERROR", "While reading file [%s]: %s (%d)",
                    _dect_in_fifo, strerror(errnum), errnum);
            goto done;
        }

        buf_fill += (size_t)bytes;

        if (buf_fill == BUF_LEN) {
            TSL_BUG_IF_FAILED(dect_process_buf(chan, buf, buf_fill));
            buf_fill = 0;
        }
    }

    ret = EXIT_SUCCESS;
done:
    return ret;
}

