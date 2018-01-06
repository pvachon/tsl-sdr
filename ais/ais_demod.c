#include <ais/ais_demod.h>
#include <ais/ais_demod_priv.h>

#include <tsl/safe_alloc.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/hexdump.h>

#include <string.h>

static
void _ais_demod_detect_reset(struct ais_demod_detect *detect)
{
    memset(detect->preambles, 0, sizeof(detect->preambles));
    memset(detect->prior_sample, 0, sizeof(detect->prior_sample));
    detect->next_field = 0;
}

static
void _ais_demod_rx_reset(struct ais_demod_rx *rx)
{
    memset(rx->packet, 0, sizeof(rx->packet));
    rx->current_bit = 0;
    rx->nr_ones = 0;
}

aresult_t ais_demod_new(struct ais_demod **pdemod, ais_demod_on_message_callback_func_t cb, uint32_t freq)
{
    aresult_t ret = A_OK;

    struct ais_demod *demod = NULL;

    TSL_ASSERT_ARG(NULL != pdemod);
    TSL_ASSERT_ARG(NULL != cb);

    *pdemod = NULL;

    if (FAILED(ret = TZAALLOC(demod, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    _ais_demod_rx_reset(&demod->packet_rx);
    _ais_demod_detect_reset(&demod->detector);

    demod->on_msg_cb = cb;
    demod->freq = freq;
    demod->state = AIS_DEMOD_STATE_SEARCH_SYNC;

    *pdemod = demod;

done:
    if (FAILED(ret)) {
        if (NULL != demod) {
            TFREE(demod);
        }
    }
    return ret;
}

aresult_t ais_demod_delete(struct ais_demod **pdemod)
{
    aresult_t ret = A_OK;

    struct ais_demod *demod = NULL;

    TSL_ASSERT_ARG(NULL != pdemod);
    TSL_ASSERT_ARG(NULL != *pdemod);

    demod = *pdemod;

    TFREE(demod);

    *pdemod = NULL;

    return ret;
}

static inline
void _ais_demod_detect_handle_sample(struct ais_demod *demod, int16_t sample)
{
    struct ais_demod_detect *detector = NULL;

    uint8_t sample_slice = 0,
            nr_match = 0,
            last_bit = 0;

    TSL_BUG_ON(NULL == demod);

    detector = &demod->detector;

    /* Slice our new sample */
    sample_slice = sample > 0;

    /* Grab our prior sample */
    last_bit = detector->prior_sample[detector->next_field];

    /* Record this sample */
    detector->prior_sample[detector->next_field] = sample_slice;

    detector->preambles[detector->next_field] <<= 1;
    detector->preambles[detector->next_field] |= !(last_bit ^ sample_slice);

    for (size_t i = 0; i < AIS_DECIMATION_RATE; i++) {
        if (detector->preambles[i] == 0x5555557eul) {
            DIAG("   Preamble [%zu]: 0x%08x", i, detector->preambles[i]);
            nr_match++;
        }
    }

    if (nr_match >= 3) {
        /* We have a packet */
        DIAG("SEARCH_SYNC -> RECEIVING (%d matches)", (int)nr_match);
        demod->state = AIS_DEMOD_STATE_RECEIVING;
        demod->sample_skip = 2;
        _ais_demod_rx_reset(&demod->packet_rx);
        demod->packet_rx.last_sample = detector->prior_sample[detector->next_field];
    }

    detector->next_field = (detector->next_field + 1) % AIS_DECIMATION_RATE;
}

static inline
void _ais_demod_packet_rx_sample(struct ais_demod *demod, int16_t sample)
{
    struct ais_demod_rx *rx = NULL;

    uint8_t bit = 0,
            raw = 0,
            last = 0;

    TSL_BUG_ON(NULL == demod);

    rx = &demod->packet_rx;

    raw = sample > 0;
    last = rx->last_sample;

    bit = !(last ^ raw);
    rx->last_sample = raw;

    if (rx->nr_ones < 5) {
        rx->packet[rx->current_bit / 8] |= bit << (rx->current_bit % 8);
        rx->current_bit++;
    } else {
        DIAG("Stuffed bit removed (would have been %u)", bit);
    }

    if (0 == bit) {
        rx->nr_ones = 0;
    } else {
        rx->nr_ones++;
    }

    if (AIS_PACKET_DATA_BITS + AIS_PACKET_FCS_BITS == rx->current_bit) {
        hexdump_dump_hex(rx->packet, (AIS_PACKET_DATA_BITS + AIS_PACKET_FCS_BITS)/8);
        DIAG("RECEIVING -> SEARCH_SYNC");
        demod->state = AIS_DEMOD_STATE_SEARCH_SYNC;
        demod->sample_skip = 0;
        _ais_demod_detect_reset(&demod->detector);
    }
}

aresult_t ais_demod_on_pcm(struct ais_demod *demod, const int16_t *samples, size_t nr_samples)
{
    aresult_t ret = A_OK;

    size_t cur_sample = 0;

    TSL_ASSERT_ARG(NULL != demod);
    TSL_ASSERT_ARG(NULL != samples);

    while (nr_samples > cur_sample) {
        if (demod->state == AIS_DEMOD_STATE_SEARCH_SYNC) {
            for (size_t i = cur_sample; i < nr_samples; i++, cur_sample++) {
                /* Process this sample */
                _ais_demod_detect_handle_sample(demod, samples[i]);
                if (demod->state == AIS_DEMOD_STATE_RECEIVING) {
                    /* Preamble was found, break. */
                    fprintf(stderr, "   %zu, %d       %% last preamble bit\n", i, samples[i]);
                    cur_sample = i + 1;
                    break;
                }
            }
        } else if (demod->state == AIS_DEMOD_STATE_RECEIVING) {
            for (size_t i = cur_sample; i < nr_samples; i++, cur_sample++) {
                if ((demod->sample_skip++ % AIS_DECIMATION_RATE) == 0) {
                    fprintf(stderr, "  %zu, %d %% skip = %zu\n", i, samples[i], demod->sample_skip - 1);
                    _ais_demod_packet_rx_sample(demod, samples[i]);
                    if (demod->state == AIS_DEMOD_STATE_SEARCH_SYNC) {
                        cur_sample = i + 1;
                        break;
                    }
                }
            }
        } else {
            PANIC("Unknown state for AIS demodulator: %d", demod->state);
        }
    }

    return ret;
}

