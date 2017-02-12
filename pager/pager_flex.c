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

static inline int8_t _pager_flex_slice_2fsk(struct pager_flex *flex, int16_t sample);
static inline int8_t _pager_flex_slice_4fsk(struct pager_flex *flex, int16_t sample);

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
        .seq_a = 0x4f79,
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
void _pager_flex_phase_process(struct pager_flex *flex, unsigned phase_id)
{
    struct pager_flex_block *blk = NULL;
    struct pager_flex_phase *phs = NULL;
    uint32_t biw = 0;
    uint8_t biw_cksum = 0,
            biw_prio = 0,
            biw_eob = 0,
            biw_vsw = 0,
            biw_carry = 0,
            biw_m = 0;
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
        DIAG("PHASE %u: Skipping (could not correct BIW %08x)", phase_id, biw);
        goto done;
    }

    uint32_t biw_masked = biw & 0x1fffff;
    for (size_t nibble = 0; nibble < 6; nibble++) {
        biw_cksum += biw_masked & 0xf;
        biw_masked >>= 4;
    }
    biw_cksum &= 0xf;

    biw_prio = (biw >> 4) & 0xf;
    biw_eob = (biw >> 8) & 0x3;
    biw_vsw = (biw >> 10) & 0x3f;
    biw_carry = (biw >> 16) & 0x3;
    biw_m = (biw >> 18) & 0x7;
    DIAG("PHASE %u: BIW: %08x (CkSum:%01x Prio:%01x EoB:%01x VSW:%02x Carry:%01x Collapse:%01x)",
            phase_id, biw, biw_cksum, biw_prio, biw_eob, biw_vsw, biw_carry, biw_m);

    if (0 != biw_eob) {
        /* TODO: walk any additional Block Information Words */
    }

    addr_start += biw_eob;

    /* Walk the address words, and decode them */
    for (size_t i = addr_start; i < biw_vsw; i++) {
        if (bch_code_decode(flex->bch, &phs->phase_words[i])) {
            DIAG("PHASE %u: Failed to fix address word at offset %zu, we have to abort", phase_id, i);
            goto done;
        }
        phs->phase_words[i] &= 0x1fffff;

        /* Check if this is a long or short address, and decode the CAPCODE */

        /* Find the associated vector word */

        /* Decode per what the vector word indicates */
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

