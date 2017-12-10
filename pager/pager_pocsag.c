/*
 *  pager_pocsag.c - POCSAG protocol handler
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
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
#include <pager/pager_pocsag.h>
#include <pager/pager_pocsag_priv.h>
#include <pager/mueller_muller.h>
#include <pager/bch_code.h>

#include <tsl/safe_alloc.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

static
bool __pager_pocsag_check_sync_word(uint32_t word)
{
    return (__builtin_popcount(word ^ POCSAG_SYNC_CODEWORD) <= 4);
}

static
void _pager_pocsag_batch_reset(struct pager_pocsag_batch *batch)
{
    memset(batch->current_batch, 0, sizeof(batch->current_batch));
    batch->current_batch_word = 0;
    batch->current_batch_word_bit = 0;
    batch->cur_sample_skip = 0;
    batch->bit_count = 0;
}

static
void _pager_pocsag_sync_search_reset(struct pager_pocsag_sync_search *sync)
{
    sync->cur_sample_skip = 0;
    sync->nr_sync_bits = 0;
    sync->sync_word = 0;
}

static
aresult_t _pager_pocsag_baud_on_sample(struct pager_pocsag *pocsag, struct pager_pocsag_baud_detect *det, int16_t sample)
{
    aresult_t ret = A_OK;

    int bit = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != pocsag);
    TSL_ASSERT_ARG_DEBUG(NULL != det);

    bit = sample < 0 ? 1 : 0;

    det->eye_detect[det->cur_word] <<= 1;
    det->eye_detect[det->cur_word] |= bit;

    if (__pager_pocsag_check_sync_word(det->eye_detect[det->cur_word])) {
        det->nr_eye_matches++;
    } else {
        /* Check if our eye is open wide enough */
        if (det->nr_eye_matches > det->samples_per_bit/2) {
            /* Advance the state */
            DIAG("SEARCH -> SYNCHRONIZED: Initial Sync Found, skip = %u, matches = %u",
                    (unsigned)det->samples_per_bit, (unsigned)det->nr_eye_matches);
            pocsag->sample_skip = det->samples_per_bit;
            _pager_pocsag_batch_reset(&pocsag->batch);
            pocsag->batch.cur_sample_skip = det->nr_eye_matches/2;
            pocsag->cur_state = PAGER_POCSAG_STATE_SYNCHRONIZED;
        } else {
            /* No eye. */
            det->nr_eye_matches = 0;
        }
    }
    det->cur_word = (det->cur_word + 1) % det->samples_per_bit;

    return ret;
}

static
void _pager_pocsag_baud_reset(struct pager_pocsag_baud_detect *det)
{
    TSL_BUG_ON(NULL == det);
    memset(det->eye_detect, 0, sizeof(uint32_t) * det->samples_per_bit);
    det->cur_word = 0;
    det->nr_eye_matches = 0;
}

static
void _pager_pocsag_baud_search_reset(struct pager_pocsag *pocsag)
{
    _pager_pocsag_baud_reset(pocsag->baud_512);
    pocsag->baud_512->samples_per_bit = 75;
    _pager_pocsag_baud_reset(pocsag->baud_1200);
    pocsag->baud_1200->samples_per_bit = 32;
    _pager_pocsag_baud_reset(pocsag->baud_2400);
    pocsag->baud_2400->samples_per_bit = 16;
}

aresult_t pager_pocsag_new(struct pager_pocsag **ppocsag, uint32_t freq_hz,
        pager_pocsag_on_numeric_msg_func_t on_numeric,
        pager_pocsag_on_alpha_msg_func_t on_alpha)
{
    aresult_t ret = A_OK;

    /* BCH Generator Polynomial */
    int poly[6] = { 1, 0, 1, 0, 0, 1 };
    struct pager_pocsag *pocsag = NULL;

    TSL_ASSERT_ARG(NULL != ppocsag);

    if (FAILED(ret = TZAALLOC(pocsag, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    if (FAILED(ret = TACALLOC((void **)&pocsag->baud_512, 1, sizeof(struct pager_pocsag_baud_detect) + 75 * sizeof(uint32_t), SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    if (FAILED(ret = TACALLOC((void **)&pocsag->baud_1200, 1, sizeof(struct pager_pocsag_baud_detect) + 32 * sizeof(uint32_t), SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    if (FAILED(ret = TACALLOC((void **)&pocsag->baud_2400, 1, sizeof(struct pager_pocsag_baud_detect) + 16 * sizeof(uint32_t), SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    TSL_BUG_IF_FAILED(bch_code_new(&pocsag->bch, poly, 5, 31, 21, 2));

    _pager_pocsag_baud_search_reset(pocsag);

    *ppocsag = pocsag;

done:
    if (FAILED(ret)) {
        if (NULL != pocsag) {
            if (NULL != pocsag->baud_512) {
                TFREE(pocsag->baud_512);
            }
            if (NULL != pocsag->baud_1200) {
                TFREE(pocsag->baud_1200);
            }
            if (NULL != pocsag->baud_2400) {
                TFREE(pocsag->baud_2400);
            }
            if (NULL != pocsag->bch) {
                bch_code_delete(&pocsag->bch);
            }
            TFREE(pocsag);
        }
    }
    return ret;
}

aresult_t pager_pocsag_delete(struct pager_pocsag **ppocsag)
{
    aresult_t ret = A_OK;

    struct pager_pocsag *pocsag = NULL;

    TSL_ASSERT_ARG(NULL != ppocsag);
    TSL_ASSERT_ARG(NULL != *ppocsag);

    pocsag = *ppocsag;

    if (NULL != pocsag->baud_512) {
        TFREE(pocsag->baud_512);
    }
    if (NULL != pocsag->baud_1200) {
        TFREE(pocsag->baud_1200);
    }
    if (NULL != pocsag->baud_2400) {
        TFREE(pocsag->baud_2400);
    }
    if (NULL != pocsag->bch) {
        bch_code_delete(&pocsag->bch);
    }

    TFREE(pocsag);

    *ppocsag = NULL;

    return ret;
}

aresult_t pager_pocsag_on_pcm(struct pager_pocsag *pocsag, const int16_t *pcm_samples, size_t nr_samples)
{
    aresult_t ret = A_OK;

    size_t next_sample = 0;
    struct pager_pocsag_batch *batch = NULL;
    struct pager_pocsag_sync_search *sync = NULL;

    TSL_ASSERT_ARG(NULL != pocsag);
    TSL_ASSERT_ARG(NULL != pcm_samples);
    TSL_ASSERT_ARG(0 != nr_samples);

    batch = &pocsag->batch;
    sync = &pocsag->sync;

    DIAG("Starting block, length %zu", nr_samples);

    while (nr_samples > next_sample) {
        switch (pocsag->cur_state) {
        case PAGER_POCSAG_STATE_SEARCH:
            for (size_t i = 0; nr_samples > next_sample; i++) {
                TSL_BUG_IF_FAILED(_pager_pocsag_baud_on_sample(pocsag, pocsag->baud_512,
                            pcm_samples[next_sample]));
                TSL_BUG_IF_FAILED(_pager_pocsag_baud_on_sample(pocsag, pocsag->baud_1200,
                            pcm_samples[next_sample]));
                TSL_BUG_IF_FAILED(_pager_pocsag_baud_on_sample(pocsag, pocsag->baud_2400,
                            pcm_samples[next_sample]));

                next_sample++;
                if (pocsag->cur_state == PAGER_POCSAG_STATE_SYNCHRONIZED) {
                    break;
                }
            }
            break;
        case PAGER_POCSAG_STATE_SYNCHRONIZED:
            pocsag->cur_state = PAGER_POCSAG_STATE_BATCH_RECEIVE;
            DIAG("SYNCHRONIZED -> BATCH_RECEIVE");
        case PAGER_POCSAG_STATE_BATCH_RECEIVE:
            DIAG("BATCH_RECEIVE: starting with %zu samples", nr_samples - next_sample);
            for (size_t i = 0; nr_samples > next_sample; i++) {
                if (++batch->cur_sample_skip == pocsag->sample_skip) {
                    int sample = pcm_samples[next_sample];
                    uint32_t bit = sample < 0 ? 1 : 0;
                    batch->current_batch[batch->current_batch_word] |= bit << batch->bit_count;
                    batch->current_batch_word_bit++;
                    batch->bit_count++;
                    batch->cur_sample_skip = 0;

                    //fprintf(stdout, " %zu, %d; \n", next_sample, (int)sample);

                    if (batch->current_batch_word_bit == 32) {
                        batch->current_batch_word_bit = 0;
                        batch->current_batch_word++;
                        if (batch->current_batch_word == PAGER_POCSAG_BATCH_BITS/32) {
                            /* Process the batch */
                            for (size_t z = 0; z < PAGER_POCSAG_BATCH_BITS/32; z++) {
                                uint32_t corrected = batch->current_batch[z] & 0x7ffffffful;
                                bool calc_parity = !(__builtin_popcount(batch->current_batch[z]) & 1);

                                if (bch_code_decode(pocsag->bch, &corrected)) {
                                    corrected = 0xfffffffful;
                                }
                                DIAG(" %zu: 0x%08x -> %08x Parity: %s", z, batch->current_batch[z], corrected, calc_parity ? "good" : "bad");
                            }

                            /* Switch to sync search state */
                            DIAG("BATCH_RECEIVE -> SEARCH_SYNCWORD (bit count = %u)", (unsigned)batch->bit_count);
                            pocsag->cur_state = PAGER_POCSAG_STATE_SEARCH_SYNCWORD;

                            batch->current_batch_word_bit = 0;
                            batch->current_batch_word = 0;

                            _pager_pocsag_sync_search_reset(sync);
                            next_sample++;
                            break;
                        }
                    }
                }

                next_sample++;
            }
            break;
        case PAGER_POCSAG_STATE_SEARCH_SYNCWORD:
            DIAG("SEARCH_SYNCWORD: Skipping at rate %u", (unsigned)pocsag->sample_skip);
            for (size_t i = 0; nr_samples > next_sample; i++) {
                if (++sync->cur_sample_skip == pocsag->sample_skip) {
                    int sample = pcm_samples[next_sample];

                    sync->cur_sample_skip = 0;
                    sync->sync_word <<= 1;
                    sync->sync_word |= (sample < 0 ? 1 : 0);
                    sync->nr_sync_bits++;

                    if (sync->nr_sync_bits == 32) {
                        if (false == __pager_pocsag_check_sync_word(sync->sync_word)) {
                            /* Search for the next sync word from scratch */
                            DIAG("SEARCH_SYNCWORD -> SEARCH (got %08x)", sync->sync_word);
                            pocsag->cur_state = PAGER_POCSAG_STATE_SEARCH;
                            pocsag->sample_skip = 0;
                            _pager_pocsag_baud_search_reset(pocsag);
                        } else {
                            DIAG("SEARCH_SYNCWORD -> BATCH_RECEIVE");
                            pocsag->cur_state = PAGER_POCSAG_STATE_BATCH_RECEIVE;
                            _pager_pocsag_batch_reset(batch);
                        }
                        next_sample++;
                        break;
                    }
                }
                next_sample++;
            }
            break;
        }
    }

    return ret;
}

