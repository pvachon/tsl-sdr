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
#include <pager/pager_flex_priv.h>
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
#include <inttypes.h>

static inline int8_t _pager_flex_slice_2fsk(struct pager_flex *flex, int16_t sample);
static inline int8_t _pager_flex_slice_4fsk(struct pager_flex *flex, int16_t sample);

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
        .sample_fudge = 0,
        .symbols_per_block = 2816,
        .nr_phases = 1,
    },
    {
        .seq_a = 0x84e7,
        .baud = 3200,
        .fsk_levels = 2,
        .sample_skip = 4,
        .sync_2_samples = 24,
        .sym_bits = 1,
        .slice = _pager_flex_slice_2fsk,
        .sample_fudge = 2,
        .symbols_per_block = 5632,
        .nr_phases = 2,
    },
    {
        .seq_a = 0x4f97,
        .baud = 3200,
        .fsk_levels = 4,
        .sample_skip = 9,
        .sync_2_samples = 12,
        .sym_bits = 2,
        .slice = _pager_flex_slice_4fsk,
        .sample_fudge = 0,
        .symbols_per_block = 2816,
        .nr_phases = 2,
    },
    {
        .seq_a = 0x215f,
        .baud = 6400,
        .fsk_levels = 4,
        .sample_skip = 4,
        .sync_2_samples = 32,
        .sym_bits = 2,
        .slice = _pager_flex_slice_4fsk,
        .sample_fudge = 2,
        .symbols_per_block = 5632,
        .nr_phases = 4,
    },
};

/**
 * The number of pager codings we support
 */
#define NR_PAGER_CODINGS (sizeof(_pager_codings)/sizeof(struct pager_flex_coding))

/**
 * Check the standard checksum for relevant FLEX words. Returns
 * the checksum value for the given word.
 */
static inline
uint8_t __pager_flex_calc_word_checksum(uint32_t word)
{
    uint8_t cksum = 0;
    word &= 0x1fffff;

    for (size_t nibble = 0; nibble < 6; nibble++) {
        cksum += word & 0xf;
        word >>= 4;
    }

    return cksum & 0xf;
}

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

    /* Adjust the sample per the offset determined during the sync phase */
    sample -= flex->sample_delta;

    if (sample < 0) {
        if (-sample > flex->sample_range/4) {
            return 0;
        } else {
            return 1;
        }
    } else {
        if (sample > flex->sample_range/4) {
            return 2;
        } else {
            return 3;
        }
    }
}

static
void _pager_flex_block_reset(struct pager_flex_block *block)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == block);
#endif

    /* Reset the block state */
    block->nr_symbols = 0;
    block->phase_ff = false;

    for (size_t i = 0; i < PAGER_FLEX_PHASE_MAX; i++) {
        struct pager_flex_phase *ph = &block->phase[i];
        ph->cur_bit = 0;
        ph->cur_word = 0;
        ph->base_word = 0;
    }

#ifdef _TSL_DEBUG
    memset(&block->phase, 0, sizeof(block->phase));
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
    sync2->inv_c = 0;
    sync2->nr_c = 0;
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

    sync->range_avg_sum_high = 0;
    sync->range_avg_sum_low = 0;
    sync->range_avg_count_high = 0;
    sync->range_avg_count_low = 0;
}

/**
 * Reset the flex pager decoder to the SYNC state
 */
static
void _pager_flex_reset_sync(struct pager_flex *flex)
{
#ifdef _TSL_DEBUG

#endif
    /* Stick to 1 sample/symbol, used in searching for the sync word */
    flex->symbol_samples = 1;

    flex->state = PAGER_FLEX_STATE_SYNC_1;

    flex->skip = 0;
    flex->skip_count = 0;

    flex->sample_range = 0;
    flex->sample_delta = 0;

    flex->frame_id = 0;
    flex->cycle_id = 0;

    /* Clear the sync trackers and block state trackers */
    _pager_flex_sync_reset(&flex->sync);
    _pager_flex_sync_2_reset(&flex->sync_2);
    _pager_flex_block_reset(&flex->block);
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
void _pager_flex_sync_update(struct pager_flex *flex, int16_t sample)
{
    struct pager_flex_sync *sync = NULL;
    int8_t symbol = INT8_MIN;
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
#endif

    sync = &flex->sync;

    sync->sample_counter = (sync->sample_counter + 1) % 10;
    symbol = _pager_flex_slice_2fsk(flex, sample);

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
            }
            sync->bit_counter = 0;
        }
        break;

    case PAGER_FLEX_SYNC_STATE_A:
        if (0 == sync->sample_counter) {
            sync->a <<= 1;
            sync->a |= !!symbol;

            if (sample > 0) {
                sync->range_avg_sum_high += sample;
                sync->range_avg_count_high++;
            } else {
                sync->range_avg_sum_low += sample;
                sync->range_avg_count_low++;
            }

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

            if (sample > 0) {
                sync->range_avg_sum_high += sample;
                sync->range_avg_count_high++;
            } else {
                sync->range_avg_sum_low += sample;
                sync->range_avg_count_low++;
            }

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

            if (sample > 0) {
                sync->range_avg_sum_high += sample;
                sync->range_avg_count_high++;
            } else {
                sync->range_avg_sum_low += sample;
                sync->range_avg_count_low++;
            }

            sync->bit_counter++;
            if (32 == sync->bit_counter) {
                if (_pager_flex_sync_check_baud(sync)) {
                    DIAG("INV_A -> FIW INV_A = %08x INV_A_BAR= %08x", sync->inv_a, ~sync->inv_a);
                    sync->state = PAGER_FLEX_SYNC_STATE_FIW;
                } else {
                    PAG_MSG(SEV_WARNING, "UNKNOWN-BAUD", "Unknown baud identifier code: %04x/%04x", sync->a, sync->inv_a);
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
            sync->fiw |= !!symbol << 31;

            if (sample > 0) {
                sync->range_avg_sum_high += sample;
                sync->range_avg_count_high++;
            } else {
                sync->range_avg_sum_low += sample;
                sync->range_avg_count_low++;
            }

            sync->bit_counter++;
            if (32 == sync->bit_counter) {
                /* Calculate the swing range for the signal. We have to do this heuristically
                 * for 4FSK unfortunately.
                 */
                int16_t slice_range_high = sync->range_avg_sum_high/(int)sync->range_avg_count_high,
                        slice_range_low = sync->range_avg_sum_low/(int)sync->range_avg_count_low;

                flex->sample_range = slice_range_high - slice_range_low;
                flex->sample_delta = slice_range_high - (int)flex->sample_range/2;

                DIAG("FIW -> SYNCED (FIW: %08x sliceHi: %d sliceLo: %d sliceRange: %d, sampleDelta: %d",
                        sync->fiw, slice_range_high, slice_range_low, flex->sample_range,
                        flex->sample_delta);

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
        sync->nr_dots++;

        if (coding->sync_2_samples == sync->nr_dots) {
            DIAG("PAGER_FLEX_SYNC_2_STATE_COMMA -> PAGER_FLEX_SYNC_2_STATE_C");
            /* And we're off to state C */
            sync->state = PAGER_FLEX_SYNC_2_STATE_C;
        }

        break;
    case PAGER_FLEX_SYNC_2_STATE_C: {
            int8_t sym = coding->slice(flex, sample);
            sync->c <<= coding->sym_bits;
            sync->c |= sym;

            sync->nr_c += coding->sym_bits;

            if (sync->nr_c == 16) {
                DIAG("PAGER_FLEX_SYNC_2_STATE_C -> PAGER_FLEX_SYNC_2_STATE_INV_COMMA (c = 0x%02x)", sync->c);
                sync->state = PAGER_FLEX_SYNC_2_STATE_INV_COMMA;
                sync->nr_dots = 0;
            }
        }
        break;
    case PAGER_FLEX_SYNC_2_STATE_INV_COMMA:
        sync->nr_dots++;

        if (coding->sync_2_samples == sync->nr_dots) {
            DIAG("PAGER_FLEX_SYNC_2_STATE_INV_COMMA -> PAGER_FLEX_SYNC_2_STATE_INV_C");
            sync->state = PAGER_FLEX_SYNC_2_STATE_INV_C;
            sync->nr_c = 0;
        }
        break;
    case PAGER_FLEX_SYNC_2_STATE_INV_C: {
            int8_t sym = coding->slice(flex, sample);
            sync->inv_c <<= coding->sym_bits;
            sync->inv_c |= sym;
            sync->nr_c += coding->sym_bits;

            if (sync->nr_c == 16) {
                sync->state = PAGER_FLEX_SYNC_2_STATE_SYNCED;
                /* TODO: we should check C, INV_C and generate a warning if they're bad at this point. */
            }
        }
        break;
    case PAGER_FLEX_SYNC_2_STATE_SYNCED:
        PANIC("Should not get to PAGER_FLEX_SYNC_2_STATE_SYNCED");
    }
}

static
aresult_t _pager_flex_decode_address(struct pager_flex *flex, uint32_t *addr, uint64_t *pcapcode, size_t *pnr_words)
{
    aresult_t ret = A_OK;

    uint32_t addr_first = 0,
             addr_second = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != flex);
    TSL_ASSERT_ARG_DEBUG(NULL != addr);
    TSL_ASSERT_ARG_DEBUG(NULL != pcapcode);
    TSL_ASSERT_ARG_DEBUG(NULL != pnr_words);

    *pcapcode = 0;
    *pnr_words = 0;

    /* Correct the first word */
    if (bch_code_decode(flex->bch, &addr[0])) {
        ret = A_E_INVAL;
        goto done;
    }
    addr_first = addr[0] &= 0x1fffff;

    /* Check if this is a long or short address, and decode the CAPCODE */
    if ( (addr_first > 0x8000 && addr_first <= 0x1e0000) ||
            (addr_first > 0x1f0000 && addr_first < 0x1f7fff) )
    {
        /* Short address, decode as such, set the number of output words */
        *pcapcode = addr_first - 32768;
        *pnr_words = 0;
    } else {
        /* Correct the second word */
        if (bch_code_decode(flex->bch, &addr[1])) {
            ret = A_E_INVAL;
            goto done;
        }

        /* Calculate the second address, per the Binary -> CAPCODE decoding scheme */
        addr_second = addr[1] &= 0x1fffff;
        *pnr_words = 1;
        *pcapcode = 0x1f9001 + (((0x1fffff - addr_second) * 32768) + addr_first - 1);
        /* TODO: this only covers 1-2 type long capcodes */
    }

done:
    return ret;
}

/**
 * For debug purposes, the message vector type code, so we can print something human readable.
 */
static const
char *__pager_flex_type_code[] = {
    [0x0] = "SEC",
    [0x1] = "SIV",
    [0x2] = "TON",
    [0x3] = "NUM",
    [0x4] = "SNM",
    [0x5] = "ALN",
    [0x6] = "HEX",
    [0x7] = "NNM",
};

/**
 * Parse an alphanumeric vector. If long_word is -1, all words come from the words array. Otherwise, we'll
 * start with the long_word for processing the message body.
 *
 * Since the first word of an Alphanumeric message is always the state flags, decoding doesn't require any
 * special footwork around the long word.
 */
static
aresult_t _pager_flex_decode_alphanumeric(struct pager_flex *flex, uint8_t phase, uint64_t capcode, uint32_t long_word, uint32_t *words, size_t nr_words)
{
    aresult_t ret = A_OK;

    struct pager_flex_coding *coding = NULL;
    size_t first_char_word = 1;
    int skip_word = 0;
    uint32_t status_word = 0;
    uint8_t seq_num = 0;
    bool maildrop = false,
         fragment = false;

    TSL_ASSERT_ARG_DEBUG(NULL != flex);
    TSL_ASSERT_ARG_DEBUG(NULL != words);
    TSL_ASSERT_ARG_DEBUG(0 != nr_words);

    coding = flex->sync.coding;

    TSL_BUG_ON(NULL == coding);

    if (0xfffffffful != long_word) {
        first_char_word = 0;
        status_word = long_word;
    } else {
        first_char_word = 1;
        status_word = words[0];

        if (bch_code_decode(flex->bch, &status_word)) {
            ret = A_E_INVAL;
            goto done;
        }
    }

    /* Check if this message is fragmented */
    if (status_word & (1 << 10)) {
        fragment = true;
    }

    seq_num = (status_word >> 11) & 0x3;

    /* Check if this is the first fragment. */
    if (seq_num == 3) {
        skip_word = 1;
        maildrop = !!(status_word & (1 << 20));
    }

    /* Iterate through the packed 7-bit ASCII characters */
    for (size_t i = first_char_word; i < nr_words; i++) {
        uint32_t codeword = words[i];
        if (bch_code_decode(flex->bch, &codeword)) {
            ret = A_E_INVAL;
            goto done;
        }

        if (0 != skip_word) {
            codeword >>= 7;
        }

        for (size_t j = skip_word; j < 3; j++) {
            uint8_t ch = codeword & 0x7f;
            if (ch != 0x3) {
                flex->msg_buf[flex->msg_len++] = ch;
            } else {
                break;
            }

            if (flex->msg_len == 255) {
                break;
            }
            codeword >>= 7;
        }
        skip_word = 0;

        if (flex->msg_len == 255) {
            break;
        }
    }

    ret = flex->on_alnum_msg(flex, coding->baud, phase, flex->cycle_id, flex->frame_id, capcode,
            fragment, maildrop, seq_num, flex->msg_buf, flex->msg_len, flex->freq_hz);

done:
    return ret;
}

/**
 * LUT for converting a binary-coded numeric page value to a string literal.
 */
static const
char __pager_flex_num_lut[] = {
    [0] = '0',
    [1] = '1',
    [2] = '2',
    [3] = '3',
    [4] = '4',
    [5] = '5',
    [6] = '6',
    [7] = '7',
    [8] = '8',
    [9] = '9',
    [10] = 'X',
    [11] = 'U',
    [12] = ' ',
    [13] = '-',
    [14] = ']',
    [15] = '[',
};

/**
 * Decode a numeric message.
 */
static
aresult_t _pager_flex_decode_numeric(struct pager_flex *flex, uint8_t phase, uint64_t capcode, uint32_t long_word, uint32_t *words, size_t nr_words)
{
    aresult_t ret = A_OK;

    struct pager_flex_coding *coding = NULL;
    uint32_t cur_word = 0,
             next_word = 0;
    size_t nr_bits = 0,
           cur_word_bits = 19,
           next_word_offs = 0,
           next_word_bits = 21;

    TSL_ASSERT_ARG_DEBUG(NULL != flex);
    TSL_ASSERT_ARG_DEBUG(0 != capcode);

    coding = flex->sync.coding;

    TSL_BUG_ON(NULL == coding);

    nr_bits = nr_words * 21;

    if (long_word != 0xfffffffful) {
        /* Process the initial message word first */
        cur_word = (long_word & 0x1fffff) >> 2;
        nr_bits += 19;
        cur_word_bits = 19;
        next_word_offs = 0;
    } else {
        /* Check the BCH code for cur_word */
        cur_word = words[0];
        if (bch_code_decode(flex->bch, &cur_word)) {
            ret = A_E_INVAL;
            goto done;
        }
        cur_word &= 0x1fffff;
        cur_word >>= 2;
        cur_word_bits = 19;
        nr_bits -= 2;
        next_word_offs = 1;
    }

    if (next_word_offs < nr_words) {
        next_word = words[next_word_offs];
        if (bch_code_decode(flex->bch, &next_word)) {
            ret = A_E_INVAL;
            goto done;
        }
        next_word_bits = 21;
        next_word &= 0x1fffff;
    }

    /* Round down modulo 4 for the number of bits to process */
    nr_bits = nr_bits & ~0x3;

    do {
        size_t rem_bits = cur_word_bits & ~0x3;

        for (size_t i = 0; i < rem_bits; i += 4) {
            flex->msg_buf[flex->msg_len++] = __pager_flex_num_lut[cur_word & 0xf];
            if (flex->msg_len == 255) {
                break;
            }
            cur_word >>= 4;
            cur_word_bits -= 4;
            nr_bits -= 4;
        }

        if (flex->msg_len == 255) {
            break;
        }

        if (0 != cur_word_bits && 0 != nr_bits) {
            /* Grab the next word, update the current word with the needed number of bits */
            switch (cur_word_bits) {
            case 1:
                cur_word |= (next_word & 0x7) << 1;
                next_word >>= 3;
                next_word_bits -= 3;
                break;
            case 2:
                cur_word |= (next_word & 0x3) << 2;
                next_word >>= 2;
                next_word_bits -= 2;
                break;
            case 3:
                cur_word |= (next_word & 0x1) << 3;
                next_word >>= 1;
                next_word_bits -= 1;
                break;
            }

            /* Iterate through one more time on the current word. */
            cur_word_bits = 4;
        } else if (0 == cur_word_bits && 0 != nr_bits) {
            cur_word = next_word;
            cur_word_bits = next_word_bits;
            next_word_bits = 21;
            next_word_offs++;
            if (next_word_offs < nr_words) {
                next_word = words[next_word_offs];
                if (bch_code_decode(flex->bch, &next_word)) {
                    ret = A_E_INVAL;
                    goto done;
                }
                next_word &= 0x1fffff;
            }
        }
    } while (0 != nr_bits);

    ret = flex->on_num_msg(flex, coding->baud, phase, flex->cycle_id, flex->frame_id,
            capcode, flex->msg_buf, flex->msg_len, flex->freq_hz);

done:
    return ret;
}

/**
 * Decode a tone message. Tone messages can also contain a short numeric message.
 */
static
aresult_t _pager_flex_decode_tone(struct pager_flex *flex, uint8_t phase, uint64_t capcode, uint32_t first_word, uint32_t second_word)
{
    aresult_t ret = A_OK;

    struct pager_flex_coding *coding = NULL;
    uint8_t type = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != flex);
    TSL_ASSERT_ARG_DEBUG(0 != capcode);

    coding = flex->sync.coding;

    TSL_BUG_ON(NULL == coding);

    first_word &= 0x1fffff;

    type = (first_word >> 7) & 0x3;

    switch (type) {
    case PAGER_FLEX_SHORT_TYPE_3_OR_8:
        /* Parse the first word */
        first_word >>=9;
        for (int i = 0; i < 3; i++) {
            flex->msg_buf[flex->msg_len++] = __pager_flex_num_lut[first_word & 0xf];
            first_word >>= 4;
        }
        if (0xfffffffful != second_word) {
            second_word &= 0x1fffff;
            for (int i = 0; i < 5; i++) {
                flex->msg_buf[flex->msg_len++] = __pager_flex_num_lut[second_word & 0xf];
                second_word >>= 4;
            }
        }

        /* Deliver the message as a normal alphanumeric message */
        ret = flex->on_num_msg(flex, coding->baud, phase, flex->cycle_id, flex->frame_id, capcode, flex->msg_buf, flex->msg_len, flex->freq_hz);
        break;
    case PAGER_FLEX_SHORT_TYPE_8_SOURCES:
        PAG_MSG(SEV_INFO, "TONE", "%02u/%03u/%c [ %9"PRIu64"] Sourced Tone: [%08x, %08x]", flex->cycle_id, flex->frame_id, phase + 'A', capcode, first_word, second_word);
        break;
    case PAGER_FLEX_SHORT_TYPE_SOURCES_AND_NUM:
        PAG_MSG(SEV_INFO, "TONE", "%02u/%03u/%c [ %9"PRIu64"] Sequenced Tone: [%08x, %08x]", flex->cycle_id, flex->frame_id, phase + 'A', capcode, first_word, second_word);
        break;

    case PAGER_FLEX_SHORT_TYPE_UNUSED:
    default:
        ret = A_E_INVAL;
        goto done;
    }


done:
    return ret;
}

static
aresult_t _pager_flex_decode_short_instruction_vec(struct pager_flex *flex, uint8_t phase, uint64_t capcode, uint32_t vec_word)
{
    aresult_t ret = A_OK;

    struct pager_flex_coding *coding = NULL;
    unsigned siv_type = 0,
             siv_data = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != flex);

    vec_word &= 0x7fffff;

    if (0xf != __pager_flex_calc_word_checksum(vec_word)) {
        ret = A_E_INVAL;
        goto done;
    }

    coding = flex->sync.coding;

    /* Middle 3 bits represent the instruction type */
    siv_type = (vec_word >> 7) & 0x7;

    /* Mask out the top 11 bits for instruction data */
    siv_data = (vec_word >> 10) & 0x7ff;

    switch (siv_type) {
    case PAGER_FLEX_SIV_TEMP_ADDRESS_ACTIVATION:
        break;
    case PAGER_FLEX_SIV_SYSTEM_EVENT:
        PAG_MSG(SEV_INFO, "SIV", "%02u/%03u/%c - [%9"PRIu64"] System Event (data = %08x)", flex->cycle_id, flex->frame_id, phase + 'A', capcode, siv_data);
        break;
    case PAGER_FLEX_SIV_RESERVED_TEST:
        PAG_MSG(SEV_INFO, "SIV", "%02u/%03u/%c - [%9"PRIu64"] Reserved Test (data = %08x)", flex->cycle_id, flex->frame_id, phase + 'A', capcode, siv_data);
        break;
    default:
        PAG_MSG(SEV_INFO, "SIV", "%02u/%03u/%c - [%9"PRIu64"] Unknown SIV %u (data = %08x)", flex->cycle_id, flex->frame_id, phase + 'A', capcode,
                siv_type, siv_data);
    }

    if (NULL != flex->on_siv_msg) {
        flex->on_siv_msg(flex, coding->baud, phase, flex->cycle_id, flex->frame_id, capcode,
                siv_type, siv_data, flex->freq_hz);
    }


done:
    return ret;
}

/**
 * Decode a FLEX vector information word, and emit a 
 */
static
aresult_t _pager_flex_decode_vector(struct pager_flex *flex, uint8_t phase, uint64_t capcode, uint32_t *vec, size_t nr_vec_words, uint32_t *base)
{
    aresult_t ret = A_OK;
    uint32_t vec_word = 0,
             vec_long_word = 0xfffffffful;
    uint8_t cksum = 0,
            vec_type = 0;
    size_t word_length = 0,
           word_start = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != flex);
    TSL_ASSERT_ARG_DEBUG(NULL != vec);
    TSL_ASSERT_ARG_DEBUG(0 != capcode);
    TSL_ASSERT_ARG_DEBUG(0 != nr_vec_words);

    /* Erase any previous message */
    flex->msg_len = 0;

    /* Fix the vector words we'll need, first */
    for (size_t i = 0; i < nr_vec_words; i++) {
        if (bch_code_decode(flex->bch, &vec[i])) {
            ret = A_E_INVAL;
            goto done;
        }
    }

    vec_word = vec[0];

    /* Grab the first word, check the checksum */
    if (0xf != (cksum = __pager_flex_calc_word_checksum(vec_word))) {
        ret = A_E_INVAL;
        goto done;
    }

    /* Now figure out how to interpret this guy */
    vec_type = (vec_word >> 4) & 0x7;

    /* For most vector types, there's a default start word field */
    word_start = (vec_word >> 7) & 0x7f;

    /* Adjust if the first message word is the second vector word */
    if (2 == nr_vec_words) {
        vec_long_word = vec[1];
    }

    switch (vec_type) {
    case PAGER_FLEX_MESSAGE_TONE:
        if (FAILED(_pager_flex_decode_tone(flex, phase, capcode, vec_word, vec_long_word))) {
            ret = A_E_INVAL;
            goto done;
        }
        break;
    case PAGER_FLEX_MESSAGE_STANDARD_NUMERIC:
        /* Only 3 bits of word length for numeric pages */
        word_length = ((vec_word >> 14) & 0x7) + 1;
        if (2 == nr_vec_words) {
            word_length -= 1;
        }
        if (FAILED(_pager_flex_decode_numeric(flex, phase, capcode, vec_long_word, base + word_start, word_length))) {
            ret = A_E_INVAL;
            goto done;
        }
        break;
    case PAGER_FLEX_MESSAGE_ALPHANUMERIC:
        word_length = (vec_word >> 14) & 0x7f;
        if (2 == nr_vec_words) {
            word_length -= 1;
        }

        if (FAILED(_pager_flex_decode_alphanumeric(flex, phase, capcode, vec_long_word, base + word_start, word_length))) {
            ret = A_E_INVAL;
            goto done;
        }
        break;
    case PAGER_FLEX_MESSAGE_SPECIAL_INSTRUCTION:
        if (FAILED(_pager_flex_decode_short_instruction_vec(flex, phase, capcode, vec_word))) {
            ret = A_E_INVAL;
            goto done;
        }
        break;
    case PAGER_FLEX_MESSAGE_SPECIAL_NUMERIC:
    case PAGER_FLEX_MESSAGE_SECURE:
    case PAGER_FLEX_MESSAGE_HEX:
    case PAGER_FLEX_MESSAGE_NUMBERED_NUMERIC:
        PAG_MSG(SEV_INFO, "UNSUPP-MSG", "%02u/%03u/%c [%9"PRIu64 "] Unsupported Message: %s", flex->cycle_id, flex->frame_id, phase + 'A', capcode,  __pager_flex_type_code[vec_type]);
        break;
    default:
        /* Shouldn't get here, but just in case... */
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

#define PAGER_FLEX_BIW_LOCAL_IDS            0
#define PAGER_FLEX_BIW_DATE                 1
#define PAGER_FLEX_BIW_TIME                 2
#define PAGER_FLEX_BIW_SYSTEM_INFO          5
#define PAGER_FLEX_BIW_COUNTRY              7

static
void __pager_flex_decode_extra_biw(struct pager_flex *flex, uint32_t biw)
{
    uint32_t add_biw = biw & 0x7ffffffful;

    TSL_BUG_ON(NULL == flex);

    if (0 == bch_code_decode(flex->bch, &add_biw)) {
        add_biw &= 0x1fffff;
        /* Perform Checksum */
        if (0xf != __pager_flex_calc_word_checksum(add_biw)) {
            PAG_MSG(SEV_INFO, "BLOCK", "Additional BIW failed checksumming.");
        } else {
            uint32_t function = (add_biw >> 4) & 0x7;
            switch (function) {
            case PAGER_FLEX_BIW_LOCAL_IDS:
                PAG_MSG(SEV_INFO, "BLOCK-LOCAL-IDS", "SSID word");
                break;
            case PAGER_FLEX_BIW_DATE: {
                unsigned year =  ((add_biw >> (7 + 9)) & 0x1f) + 1994,
                         month = ((add_biw >> (7 + 4)) & 0x1f) + 1,
                         day =   ((add_biw >> 7)       & 0xf);
                PAG_MSG(SEV_INFO, "BLOCK-DATE", "%02u-%02u-%u", year, month, day);
                break;
            }
            case PAGER_FLEX_BIW_TIME: {
                unsigned hour = (add_biw >> (7 + 9)) & 0x1f,
                         minute = (add_biw >> (7 + 3)) & 0x3f,
                         second = ((add_biw >> 7) & 0x7) << 3;
                PAG_MSG(SEV_INFO, "BLOCK-TIME", "%02u:%02u:%02u", hour, minute, second);
                break;
            }
            case PAGER_FLEX_BIW_SYSTEM_INFO:
                PAG_MSG(SEV_INFO, "BLOCK-SYS-INFO", "System Information Field");
                break;
            case PAGER_FLEX_BIW_COUNTRY:
                PAG_MSG(SEV_INFO, "BLOCK-SYS-COUNTRY", "Country Information");
                break;
            default:
                PAG_MSG(SEV_INFO, "BLOCK", "Unknown function %u.", function);
            }
        }
    } else {
        PAG_MSG(SEV_INFO, "BLOCK", "Additional BIW could not be corrected.");
    }
}

static
void _pager_flex_phase_process(struct pager_flex *flex, unsigned phase_id)
{
    struct pager_flex_block *blk = NULL;
    struct pager_flex_phase *phs = NULL;
    uint32_t biw = 0;
    uint8_t biw_vsw = 0,
            biw_eob = 0;

#ifdef _TSL_DEBUG
    uint8_t biw_prio = 0,
            biw_carry = 0,
            biw_m = 0;
#endif

    size_t addr_start = 1;

#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
    TSL_BUG_ON(phase_id >= PAGER_FLEX_PHASE_MAX);
#endif

    blk = &flex->block;
    phs = &blk->phase[phase_id];

    TSL_BUG_ON(0 == phs->base_word);
    if (0 != phs->cur_bit) {
        DIAG("WARNING: current bit ID is %u", phs->cur_bit);
    }

    DIAG("PHASE %u: %u words", phase_id, phs->cur_word);

    /* Grab the BIW, and correct it */
    biw = phs->phase_words[0] & 0x7ffffffful;
    if (bch_code_decode(flex->bch, &biw)) {
        /* Skip processing the rest of this phase */
        PAG_MSG(SEV_INFO, "BAD-BIW", "%02u/%03u/%c: Skipping (could not correct BIW %08x)", flex->cycle_id, flex->frame_id,
                phase_id + 'A', biw);
        goto done;
    }

    if (0xf != __pager_flex_calc_word_checksum(biw)) {
        PAG_MSG(SEV_INFO, "BAD-BIW", "%02u/%03u/%c: Skipping - bad checksum (for BIW %08x)", flex->cycle_id, flex->frame_id,
                phase_id + 'A', biw);
        goto done;
    }

#ifdef _TSL_DEBUG
    biw_prio = (biw >> 4) & 0xf;
    biw_carry = (biw >> 16) & 0x3;
    biw_m = (biw >> 18) & 0x7;
#endif

    biw_vsw = (biw >> 10) & 0x3f;
    biw_eob = (biw >> 8) & 0x3;
    DIAG("PHASE %u: BIW: %08x (Prio:%01x EoB:%01x VSW:%02x Carry:%01x Collapse:%01x)",
            phase_id, biw, biw_prio, biw_eob, biw_vsw, biw_carry, biw_m);

    /* Check the sanity of the BIW fields we extracted */
    if (biw_eob > biw_vsw) {
        PAG_MSG(SEV_INFO, "BAD-BIW", "%02u/%03u/%c: Skipping BIW - bad vector count count of %u (EoB = %u)", flex->cycle_id,
                flex->frame_id, phase_id + 'A', biw_vsw, biw_eob);
        goto done;
    }

    if (0 != biw_eob) {
        /* TODO: walk any additional Block Information Words */
        PAG_MSG(SEV_INFO, "BLOCK", "%02u/%02u/%c BIW end of block = %u",
                flex->cycle_id, flex->frame_id, phase_id + 'A', biw_eob);
        for (size_t i = 1; i < biw_eob; i++) {
            __pager_flex_decode_extra_biw(flex, phs->phase_words[i]);
        }
    }

    addr_start += biw_eob;

    if (addr_start == biw_vsw) {
        DIAG("No Data in %02u/%02u/%c",
            flex->cycle_id, flex->frame_id, phase_id + 'A');
    }

    /* Walk the address words, and decode them */
    for (size_t i = addr_start; i < biw_vsw; i++) {
        /* Calculate the vector offset for the given address. */
        int vec_offs = i + biw_vsw - addr_start;
        uint64_t capcode = 0;
        size_t nr_words = 0;

        if (FAILED_UNLIKELY(_pager_flex_decode_address(flex, &phs->phase_words[i], &capcode, &nr_words))) {
            /* TODO: we can probably inspect the following address word, figure out if it's the upper half
             * of a long address word, and continue, skipping the bad record. For now, easy mode.
             */
            PAG_MSG(SEV_WARNING, "BCH-ERROR", "%02u/%03u/%c Address could not be corrected",
                    flex->cycle_id, flex->frame_id, phase_id + 'A');
            goto done;
        }

        /* Decode per what the vector word indicates */
        if (FAILED_UNLIKELY(_pager_flex_decode_vector(flex, phase_id, capcode, &phs->phase_words[vec_offs], nr_words + 1, phs->phase_words))) {
            /* TODO: increment an error counter */
            PAG_MSG(SEV_WARNING, "BCH-ERROR", "%02u/%03u/%c [%9"PRIu64 "] Uncorrectable Error",
                    flex->cycle_id, flex->frame_id, phase_id + 'A', capcode);
        }

        /* Add the number of additional words consumed to i */
        i += nr_words;
    }

done:
    return;
}

static
void _pager_flex_phase_append_bit(struct pager_flex_phase *phase, bool bit)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == phase);
#endif
    phase->phase_words[phase->base_word + phase->cur_word] >>= 1;
    phase->phase_words[phase->base_word + phase->cur_word] |= ((uint32_t)(!!bit)) << 31;

    phase->cur_word = (phase->cur_word + 1) % 8;

    /* Update the state of the phase tracker */
    if (0 == phase->cur_word) {
        phase->cur_bit++;
    }

    /* Start decoding the next block */
    if (32 == phase->cur_bit) {
        phase->base_word += 8;
        phase->cur_bit = 0;
        phase->cur_word = 0;
    }
}

static
void _pager_flex_block_update(struct pager_flex *flex, int16_t sample)
{
    struct pager_flex_block *blk = NULL;
    struct pager_flex_coding *coding = NULL;
    int8_t symbol = 0;
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
#endif
    blk = &flex->block;
    coding = flex->sync.coding;
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == coding);
#endif

    symbol = coding->slice(flex, sample);

    /* Put the symbol bit(s) in the right phase */
    switch (coding->nr_phases) {
    case 1:
        TSL_BUG_ON(coding->sym_bits != 1);
        /* Always phase A */
        _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_A], (1 == symbol));
        break;
    case 2:
        /* There are two phases in the current coding, Phase A and Phase C, so fill them in */
        if (2 == coding->fsk_levels) {
            struct pager_flex_phase *phase = NULL;
            /* Write alternating symbols to the appropriate phase */

            if (false == blk->phase_ff) {
                phase = &blk->phase[PAGER_FLEX_PHASE_A];
            } else {
                phase = &blk->phase[PAGER_FLEX_PHASE_C];
            }

            _pager_flex_phase_append_bit(phase, (1 == symbol));
            blk->phase_ff = !blk->phase_ff;
        } else {
            TSL_BUG_ON(coding->sym_bits != 2);
            /* Break apart the symbol */
            _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_A], !!(symbol & 2));
            _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_C], !!(symbol & 1));
        }
        break;
    case 4:
        TSL_BUG_ON(2 != coding->sym_bits);
        if (false == blk->phase_ff) {
#ifdef _DUMP_SAMPLE_CODES
        fprintf(stderr, "%d %d %d (sample = %d)\n", !!(symbol & 2), !!(symbol & 1), symbol, sample - flex->sample_delta);
#endif
            _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_A], !!(symbol & 2));
            _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_B], !!(symbol & 1));
        } else {
            _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_C], !!(symbol & 2));
            _pager_flex_phase_append_bit(&blk->phase[PAGER_FLEX_PHASE_D], !!(symbol & 1));
        }
        blk->phase_ff = !blk->phase_ff;
        break;
    default:
        PANIC("Unknown number of phases for FLEX coding: %u", coding->nr_phases);
    }

    blk->nr_symbols++;

    if (blk->nr_symbols == coding->symbols_per_block) {
        /* Process the block data, one phase at a time */
        switch (coding->nr_phases) {
        case 1:
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_A);
            break;
        case 2:
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_A);
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_C);
            break;
        case 4:
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_A);
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_B);
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_C);
            _pager_flex_phase_process(flex, PAGER_FLEX_PHASE_D);
            break;
        }

        /* Reset to the idle/Sync 1 search state */
        _pager_flex_reset_sync(flex);
    }
}

static
bool _pager_flex_handle_fiw(struct pager_flex *flex)
{
#ifdef _TSL_DEBUG
    TSL_BUG_ON(NULL == flex);
#endif /* defined(_TSL_DEBUG) */

    uint32_t fiw = flex->sync.fiw & 0x7ffffffful;
    uint8_t fiw_cksum = 0;

    /* Handle the FIW for this frame */
    if (0 != bch_code_decode(flex->bch, &fiw)) {
        /* Reset the sync state -- we couldn't correct the FIW */
        PAG_MSG(SEV_INFO, "BAD-FIW", "FIW %08x could not be corrected with BCH(31, 23).", fiw);
        return false;
    }

    DIAG("FIW: Corrected %u errors", __builtin_popcount(fiw ^ (flex->sync.fiw & 0x7ffffffful)));
    DIAG("SYNC2: %u bps, %uFSK (skip = %d)", flex->sync.coding->baud,
            flex->sync.coding->fsk_levels,
            flex->sync.coding->sample_skip);

    /* Check the FIW checksum */
    fiw_cksum = __pager_flex_calc_word_checksum(fiw);

    flex->cycle_id = (fiw >> 4) & 0xf;
    flex->frame_id = (fiw >> 8) & 0x7f;

    DIAG("FIW: FIX: CKSUM=%01x CycleNo=%01x FrameNo=%02x Roam=%s Repeat=%s CalcCksum=%02x",
            fiw & 0xf, flex->cycle_id, flex->frame_id, (fiw >> 15) & 1 ? "Yes" : "No",
            (fiw >> 16) & 1 ? "Yes" : "No", (unsigned)fiw_cksum);

    return 0xf == fiw_cksum;
}

aresult_t pager_flex_new(struct pager_flex **pflex, uint32_t freq_hz, pager_flex_on_alnum_msg_func_t on_aln_msg,
        pager_flex_on_num_msg_func_t on_num_msg, pager_flex_on_siv_msg_func_t on_siv_msg)
{
    aresult_t ret = A_OK;

    struct pager_flex *flex = NULL;
    /* BCH Generator Polynomial */
    int poly[6] = { 1, 0, 1, 0, 0, 1 };

    TSL_ASSERT_ARG(NULL != pflex);
    TSL_ASSERT_ARG(NULL != on_aln_msg);
    TSL_ASSERT_ARG(NULL != on_num_msg);

    if (FAILED(ret = TZAALLOC(flex, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    TSL_BUG_IF_FAILED(bch_code_new(&flex->bch, poly, 5, 31, 21, 2));

    flex->freq_hz = freq_hz;
    flex->on_alnum_msg = on_aln_msg;
    flex->on_num_msg = on_num_msg;
    flex->on_siv_msg = on_siv_msg;

    _pager_flex_reset_sync(flex);

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
                /* Deliver a sample to the sync state handler */
                _pager_flex_sync_update(flex, pcm_samples[i]);

                if (PAGER_FLEX_SYNC_STATE_SYNCED == flex->sync.state) {
                    if (_pager_flex_handle_fiw(flex)) {
                        DIAG("PAGER_FLEX_STATE_SYNC_1 -> PAGER_FLEX_STATE_SYNC_2");

                        flex->state = PAGER_FLEX_STATE_SYNC_2;
                        flex->skip = flex->sync.coding->sample_skip;
                        flex->skip_count = flex->skip + flex->sync.coding->sample_fudge;
                    } else {
                        /*  Reset sync state */
                        _pager_flex_reset_sync(flex);
                    }
                }
                break;

            case PAGER_FLEX_STATE_SYNC_2:
                /* Deliver the sample to the Sync 2 state handler (fast sync) */
                _pager_flex_sync2_update(flex, pcm_samples[i]);

                if (PAGER_FLEX_SYNC_2_STATE_SYNCED == flex->sync_2.state) {
                    /* Move along to processing the following block */
                    DIAG("PAGER_FLEX_STATE_SYNC_2 -> PAGER_FLEX_STATE_BLOCK");
                    flex->state = PAGER_FLEX_STATE_BLOCK;
                }

                break;

            case PAGER_FLEX_STATE_BLOCK:
                /* Update and accumulate block state */
                _pager_flex_block_update(flex, pcm_samples[i]);
                break;
            }
        } else {
            /* Skip this sample, decrement the skip counter */
            flex->skip_count--;
        }
    }

    return ret;
}

