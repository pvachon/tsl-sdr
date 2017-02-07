/*
 *  pager_flex.c - FLEX protocol handler
 *
 *  Copyright (c)2016 Phil Vachon <phil@security-embedded.com>
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

#include <pager/pager.h>
#include <pager/pager_priv.h>
#include <pager/pager_flex.h>
#include <pager/bch_code.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

struct pager_flex;
static inline int8_t _pager_flex_slice_2fsk(struct pager_flex *flex, int16_t sample);
static inline int8_t _pager_flex_slice_4fsk(struct pager_flex *flex, int16_t sample);

/**
 * Slice function using the FLEX pager state.
 */
typedef int8_t (*pager_flex_slice_sym_func_t)(struct pager_flex *flex, int16_t sample);

struct pager_flex_coding {
    /**
     * The identifier A-code sequence
     */
    uint16_t seq_a;

    /**
     * The baud rate
     */
    uint16_t baud;

    /**
     * The number of FSK levels this coding presents
     */
    uint8_t fsk_levels;

    /**
     * The number of samples we'll skip when we shift to Sync 2 and beyond.
     */
    uint8_t sample_skip;

    /**
     * Number of samples the Sync 2 phase has of the standard 0xa sequence
     */
    uint8_t sync_2_samples;

    /**
     * Number of bits in a symbol
     */
    uint8_t sym_bits;

    /**
     * Slicer function
     */
    pager_flex_slice_sym_func_t slice;
};

/**
 * Extract various fields from the Frame Information Word
 *@{
 */
#define PAGER_FLEX_FIW_CHKSUM(x)                ((x) & 0xf)
#define PAGER_FLEX_FIW_CYCLE(x)                 (((x) >> 4) & 0xf)
#define PAGER_FLEX_FIW_FRAME(x)                 (((x) >> 8) & 0x7f)
#define PAGER_FLEX_FIW_ROAMING(x)               (!!((x) & (1 << 15)))
#define PAGER_FLEX_FIW_REPEAT(x)                (!!((x) & (1 << 16)))
/**
 *@}
 */

/**
 * Various odds and ends of FLEX frame sync constants. These are used for the SYNC 1 phase
 *
 * Any FLEX frame SYNC 1 phase is structured as follows:
 * - 32-bits of 0xaaaaaaaa
 * - 16-bit A sync code leader (taken from _pager_codings)
 * - 16-bit A sync fixed magic (0x5939)
 * - 16-bit B sync fixed magic (0x5555)
 * - 16-bit A sync code leader, inverted
 * - 16-bit A sync fixed magic, inverted
 *
 * Immediately following the sync sequence is the Frame Info Word (FIW). This word is
 * protected using a BCH(31,7) code (same used for data words).
 *
 * SYNC1 is always transmitted as 1600bps, 2FSK.
 */

/**
 * Sync codes indicating the FSK mode used for SYNC 2 and beyond.
 */
static
struct pager_flex_coding _pager_codings[] = {
    {
        .seq_a = 0x78f3,
        .baud = 1600,
        .fsk_levels = 2,
        .sample_skip = 9,
        .sync_2_samples = 4,
        .sym_bits = 1,
        .slice = _pager_flex_slice_2fsk,
    },
    {
        .seq_a = 0x84e7,
        .baud = 3200,
        .fsk_levels = 2,
        .sample_skip = 4,
        .sync_2_samples = 24,
        .sym_bits = 1,
        .slice = _pager_flex_slice_2fsk,
    },
    {
        .seq_a = 0x4f79,
        .baud = 3200,
        .fsk_levels = 4,
        .sample_skip = 9,
        .sync_2_samples = 12,
        .sym_bits = 2,
        .slice = _pager_flex_slice_4fsk,
    },
    {
        .seq_a = 0x215f,
        .baud = 6400,
        .fsk_levels = 4,
        .sample_skip = 4,
        .sync_2_samples = 32,
        .sym_bits = 2,
        .slice = _pager_flex_slice_4fsk,
    },
};

/**
 * The number of pager codings we support
 */
#define NR_PAGER_CODINGS (sizeof(_pager_codings)/sizeof(struct pager_flex_coding))

/**
 * The BS1 pattern
 */
#define PAGER_FLEX_SYNC_BS1                 0xaaaaaaaaul

/**
 * Value always present in 'A' binary pattern in SYNC 1
 */
#define PAGER_FLEX_SYNC_MAGIC_A             0x5939ul

/**
 * Value always present after the intial A sync magic
 */
#define PAGER_FLEX_SYNC_MAGIC_B             0x5555ul

/**
 * Sync 2 is performed at the speed specified in the magic of Sync 1. This phase allows the
 * receiver to synchronize itself with the transmitter at the target transmission speed.
 *
 * This phase is a variable number of bits long, depending on the sync speed.
 *
 * The phase always has 2 sync phases -- BS2/C, and inv BS2/C. The latter is simply transmitted
 * as an inverse of the former.
 *
 * In our implementation, we use this phase to train the slicer for 4FSK. If the encoding is
 * 2 FSK, we'll continue to use a binary slicer.
 */

#define PAGER_FLEX_SYNC_2_MAGIC_C           0xed84

/**
 * Slice the given sample into a 2FSK symbol.
 *
 * \param flex The FLEX pager state.
 * \param sample The sample from the radio
 *
 * \return 0 if low, nonzero if high.
 */
static inline
int8_t _pager_flex_slice_2fsk(struct pager_flex *flex, int16_t sample)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
#endif

    /* Grab the sign bit and invert it */
    return !((uint16_t)sample >> 15);
}

/**
 * Slice the given sample into a 4FSK symbol.
 *
 * \param flex The FLEX pager state
 * \param sample The sample to be sliced.
 *
 * \return The symbol, per the FLEX specification
 */
static inline
int8_t _pager_flex_slice_4fsk(struct pager_flex *flex, int16_t sample)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
#endif
    return 1;

#if 0
    if (sample < 0) {
        if (-sample > flex->slice_range/2) {
            return 0;
        } else {
            return 1;
        }
    } else {
        if (sample > flex->slice_range/2) {
            return 2;
        } else {
            return 3;
        }
    }
#endif
}

static
void _pager_flex_sync_2_reset(struct pager_flex_sync_2 *sync2)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == sync2);
#endif
    sync2->state = PAGER_FLEX_SYNC_2_STATE_COMMA;
    sync2->nr_dots = 0;
    sync2->c = 0;
    sync2->nr_c = 0;
    sync2->range_avg_sum_high = 0;
    sync2->range_avg_sum_low = 0;
    sync2->range_avg_count_high = 0;
    sync2->range_avg_count_low = 0;
}

static
void _pager_flex_sync_reset(struct pager_flex_sync *sync)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == sync);
#endif

    memset(sync->sync_words, 0, sizeof(sync->sync_words));
    sync->state = PAGER_FLEX_SYNC_STATE_BS1;

    sync->sample_counter = 0;
    sync->bit_counter = 0;

    /* Clear the various words from the sync header */
    sync->a = 0;
    sync->b = 0;
    sync->inv_a = 0;
    sync->fiw = 0;
    sync->coding = NULL;
}

/**
 * Reset the flex pager decoder to the SYNC state
 */
static
void _pager_flex_reset_sync(struct pager_flex *flex)
{
#ifdef _TSL_DEBUG

#endif
    flex->symbol_counter = 0;
    flex->symbol_samples = 1;

    flex->state = PAGER_FLEX_STATE_SYNC_1;
    /* Flex is always 1600 baud initially */
    flex->baud_rate = 1600;
    flex->symbol_counter = 0;
    flex->skip = 0;
    flex->skip_count = 0;

    flex->slice_range_high = 0;
    flex->slice_range_low = 0;

    /* Clear the sync tracker */
    _pager_flex_sync_reset(&flex->sync);
    _pager_flex_sync_2_reset(&flex->sync_2);
}

static
bool _pager_flex_sync_check_baud(struct pager_flex_sync *sync)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == sync);
    TSL_BUG_ON(PAGER_FLEX_SYNC_STATE_INV_A != sync->state);
#endif
    uint16_t coding_a = (sync->a >> 16) & 0xffff,
             inv_coding_a = (sync->inv_a >> 16) & 0xffff;

    for (size_t i = 0; i < NR_PAGER_CODINGS; i++) {
        struct pager_flex_coding *coding = &_pager_codings[i];

        if (__builtin_popcount(coding->seq_a ^ coding_a) < 4 ||
                __builtin_popcount(~coding->seq_a ^ inv_coding_a) < 4)
        {
            /* Set up the configuration accordingly */
            sync->coding = coding;
            return true;
        }
    }

    return false;
}

/**
 * If we're in the initial 2FSK sync phase, update the sync word tracker and check
 * if it indicates we should move on to handling the Frame Information Word.
 *
 * This function updates the state of the FLEX sync tracker, but not of the decoder itself.
 */
static
void _pager_flex_sync_update(struct pager_flex_sync *sync, int8_t symbol)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == sync);
    TSL_BUG_ON(0 > symbol || 1 < symbol);
#endif

    sync->sample_counter = (sync->sample_counter + 1) % 10;

    switch (sync->state) {
    case PAGER_FLEX_SYNC_STATE_SEARCH_BS1:
        sync->sync_words[sync->sample_counter] <<= 1;
        sync->sync_words[sync->sample_counter] |= !!symbol;

        if (sync->sync_words[sync->sample_counter] == PAGER_FLEX_SYNC_BS1) {
            sync->bit_counter = 1;
            sync->state = PAGER_FLEX_SYNC_STATE_BS1;
            DIAG("SEARCH_BS1 -> BS1 (sample = %u)", sync->sample_counter);
        }

        break;

    case PAGER_FLEX_SYNC_STATE_BS1:
        sync->sync_words[sync->sample_counter] <<= 1;
        sync->sync_words[sync->sample_counter] |= !!symbol;

        if (sync->sync_words[sync->sample_counter] == PAGER_FLEX_SYNC_BS1) {
            sync->bit_counter++;
        } else {
            if (sync->bit_counter < 3) {
                /* We didn't actually find our sync sequence, just bad luck. */
                sync->state = PAGER_FLEX_SYNC_STATE_SEARCH_BS1;
            } else {
                /* We've seen the sync pattern, so start looking for the A mode word */
                sync->state = PAGER_FLEX_SYNC_STATE_A;

                /* Reset the sample counter to half the number of sync word counts. This
                 * becomes our sample clock.
                 */
                sync->sample_counter = sync->bit_counter / 2;

                DIAG("BS1 -> A (%u instances of BS1, eye = %u)", sync->bit_counter, sync->bit_counter);
                for (size_t i = 0; i < 10; i++) {
                    DIAG("  SW[%2zu] = 0x%08x", i, sync->sync_words[(sync->sample_counter - i) % 10]);
                }
            }
            sync->bit_counter = 0;
        }
        break;

    case PAGER_FLEX_SYNC_STATE_A:
        if (0 == sync->sample_counter) {
            sync->a <<= 1;
            sync->a |= !!symbol;

            sync->bit_counter++;
            if (32 == sync->bit_counter) {
                DIAG("A -> B A = %08x A_bar=%08x", sync->a, ~sync->a);
                sync->state = PAGER_FLEX_SYNC_STATE_B;
                sync->bit_counter = 0;
            }
        }
        break;

    case PAGER_FLEX_SYNC_STATE_B:
        if (0 == sync->sample_counter) {
            sync->b <<= 1;
            sync->b |= !!symbol;

            sync->bit_counter++;
            if (16 == sync->bit_counter) {
                DIAG("B -> INV_A B = %04x", sync->b);
                sync->state = PAGER_FLEX_SYNC_STATE_INV_A;
                sync->bit_counter = 0;
            }

        }
        break;

    case PAGER_FLEX_SYNC_STATE_INV_A:
        if (0 == sync->sample_counter) {
            sync->inv_a <<= 1;
            sync->inv_a |= !!symbol;

            sync->bit_counter++;
            if (32 == sync->bit_counter) {
                if (_pager_flex_sync_check_baud(sync)) {
                    DIAG("INV_A -> FIW INV_A = %08x INV_A_BAR= %08x", sync->inv_a, ~sync->inv_a);
                    sync->state = PAGER_FLEX_SYNC_STATE_FIW;
                } else {
                    DIAG("INV_A -> SEARCH_BS1");
                    _pager_flex_sync_reset(sync);
                }
                sync->bit_counter = 0;
            }
        }
        break;

    case PAGER_FLEX_SYNC_STATE_FIW:
        if (0 == sync->sample_counter) {
            sync->fiw >>= 1;
            sync->fiw |= (!!symbol << 31);

            sync->bit_counter++;
            if (32 == sync->bit_counter) {
                DIAG("FIW -> SYNCED FIW = %08x", sync->fiw);
                /* Check all the sync block state to make sure it lines up for Sync 1 state. */
                sync->state = PAGER_FLEX_SYNC_STATE_SYNCED;
            }
        }
        break;

    case PAGER_FLEX_SYNC_STATE_SYNCED:
        PANIC("Shouldn't have gotten to SYNC_STATE_SYNCED.");
    }

}

static
void _pager_flex_sync2_update(struct pager_flex *flex, int16_t sample)
{
    struct pager_flex_sync_2 *sync = NULL;
    struct pager_flex_coding *coding = NULL;
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
    TSL_BUG_ON(flex->state != PAGER_FLEX_STATE_SYNC_2);
#endif
    coding = flex->sync.coding;
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == coding);
#endif

    sync = &flex->sync_2;

    switch (sync->state) {
    case PAGER_FLEX_SYNC_2_STATE_COMMA:
        if (sample > 0) {
            sync->range_avg_sum_high += sample;
            sync->range_avg_count_high++;
        } else {
            sync->range_avg_sum_low += sample;
            sync->range_avg_count_low++;
        }

        sync->nr_dots++;

        if (coding->sync_2_samples == sync->nr_dots) {
            /* Calculate the swing range for the signal. */
            flex->slice_range_high = sync->range_avg_sum_high/sync->range_avg_count_high;
            flex->slice_range_low = sync->range_avg_sum_low/sync->range_avg_count_low;

            DIAG("COMMA: [%d, %d] (%u samples, %u samples)", flex->slice_range_low,
                    flex->slice_range_high, sync->range_avg_count_high,
                    sync->range_avg_count_low);

            DIAG("PAGER_FLEX_SYNC_2_STATE_COMMA -> PAGER_FLEX_SYNC_2_STATE_C");// (range = %d)", flex->slice_range);

            /* And we're off to state C */
            sync->state = PAGER_FLEX_SYNC_2_STATE_C;
        }

        break;
    case PAGER_FLEX_SYNC_2_STATE_C: {
            int8_t sym = coding->slice(flex, sample);
            DIAG("SYM: %d", sym);
            sync->c <<= coding->sym_bits;
            sync->c |= sym;

            sync->nr_c += coding->sym_bits;

            if (sync->nr_c == 16) {
                DIAG("PAGER_FLEX_SYNC_2_STATE_C -> PAGER_FLEX_SYNC_2_STATE_SYNCED (c = 0x%02x)", sync->c);
                sync->state = PAGER_FLEX_SYNC_2_STATE_SYNCED;
            }

        }
        break;
    case PAGER_FLEX_SYNC_2_STATE_INV_COMMA:
        break;
    case PAGER_FLEX_SYNC_2_STATE_INV_C:
        break;
    case PAGER_FLEX_SYNC_2_STATE_SYNCED:
        PANIC("Should not get to PAGER_FLEX_SYNC_2_STATE_SYNCED");
    }
}

static
bool _pager_flex_handle_fiw(struct pager_flex *flex)
{
    uint32_t fiw = flex->sync.fiw & 0x7ffffffful,
             fiw_masked = 0;
    uint8_t fiw_cksum = 0;

    /* Handle the FIW for this frame */
    if (0 != bch_code_decode(flex->bch, &fiw)) {
        /* Reset the sync state -- we couldn't correct the FIW */
        DIAG("Unable to correct FIW, resetting sync state.");
        _pager_flex_reset_sync(flex);
    }

    DIAG("FIW: Corrected %u errors", __builtin_popcount(fiw ^ (flex->sync.fiw & 0x7ffffffful)));
    DIAG("SYNC2: %u bps, %uFSK (skip = %d)", flex->sync.coding->baud,
            flex->sync.coding->fsk_levels,
            flex->sync.coding->sample_skip);

    /* Check the FIW checksum */
    fiw_masked = fiw & 0x1ffffful;
    for (size_t nibble = 0; nibble < 6; nibble++) {
        fiw_cksum += fiw_masked & 0xf;
        fiw_masked >>= 4;
    }
    fiw_cksum &= 0xf;

    DIAG("FIW: FIX: CKSUM=%01x CycleNo=%01x FrameNo=%02x Roam=%s Repeat=%s CalcCksum=%02x",
            fiw & 0xf, (fiw >> 4) & 0xf, (fiw >> 8) & 0x7f, (fiw >> 15) & 1 ? "Yes" : "No",
            (fiw >> 16) & 1 ? "Yes" : "No", (unsigned)fiw_cksum);

    return 0xf == fiw_cksum;
}

aresult_t pager_flex_new(struct pager_flex **pflex, uint32_t freq_hz, pager_flex_on_message_cb_t on_msg)
{
    aresult_t ret = A_OK;

    struct pager_flex *flex = NULL;
    /* BCH Generator Polynomial */
    int poly[6] = { 1, 0, 1, 0, 0, 1 };

    TSL_ASSERT_ARG(NULL != pflex);
    TSL_ASSERT_ARG(NULL != on_msg);

    if (FAILED(ret = TZAALLOC(flex, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    TSL_BUG_IF_FAILED(bch_code_new(&flex->bch, poly, 5, 31, 21, 2));

    flex->freq_hz = freq_hz;
    flex->on_msg = on_msg;

    *pflex = flex;

done:
    return ret;
}

aresult_t pager_flex_delete(struct pager_flex **pflex)
{
    aresult_t ret = A_OK;

    struct pager_flex *flex = NULL;

    TSL_ASSERT_ARG(NULL != pflex);
    TSL_ASSERT_ARG(NULL != *pflex);

    flex = *pflex;

    /* Cleanup the flex demod context */
    if (NULL != flex->bch) {
        bch_code_delete(&flex->bch);
    }

    TFREE(flex);
    *pflex = NULL;

    return ret;
}

aresult_t pager_flex_on_pcm(struct pager_flex *flex, const int16_t *pcm_samples, size_t nr_samples)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != flex);
    TSL_ASSERT_ARG(NULL != pcm_samples);
    TSL_ASSERT_ARG(0 != nr_samples);

    for (size_t i = 0; i < nr_samples; i++) {
        if (0 == flex->skip_count) {
            flex->skip_count = flex->skip;
            switch (flex->state) {
            case PAGER_FLEX_STATE_SYNC_1:
                /* Deliver a sample to the sync state handler, after treating it as a 2FSK symbol */
                _pager_flex_sync_update(&flex->sync, _pager_flex_slice_2fsk(flex, pcm_samples[i]));
                if (PAGER_FLEX_SYNC_STATE_SYNCED == flex->sync.state) {
                    if (_pager_flex_handle_fiw(flex)) {
                        DIAG("PAGER_FLEX_STATE_SYNC_1 -> PAGER_FLEX_STATE_SYNC_2");

                        flex->state = PAGER_FLEX_STATE_SYNC_2;
                        flex->skip = flex->sync.coding->sample_skip;
                        flex->skip_count = flex->skip;
                    } else {
                        /*  Reset sync state */
                        _pager_flex_reset_sync(flex);
                    }
                }
                break;

            case PAGER_FLEX_STATE_SYNC_2:
                DIAG("SAMPLE = %d", pcm_samples[i]);
                _pager_flex_sync2_update(flex, pcm_samples[i]);

#if 0
                if (pcm_samples[i] > 0) {
                    ((int16_t *)pcm_samples)[i] = INT16_MAX;
                } else {
                    ((int16_t *)pcm_samples)[i] = INT16_MIN;
                }
#endif

                if (PAGER_FLEX_SYNC_2_STATE_SYNCED == flex->sync_2.state) {
                    /* Move along to processing the following block */
                    DIAG("PAGER_FLEX_STATE_SYNC_2 -> PAGER_FLEX_STATE_BLOCK");
                    flex->state = PAGER_FLEX_STATE_BLOCK;
                }

                break;

            case PAGER_FLEX_STATE_BLOCK:
                /* For now, reset to the sync phase */
                _pager_flex_reset_sync(flex);
            }
        } else {
            /* Skip this sample, decrement the skip counter */
            flex->skip_count--;
        }
    }

    return ret;
}

