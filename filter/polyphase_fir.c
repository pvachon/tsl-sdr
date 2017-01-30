/*
 *  polyphase_fir.c - A polyphase FIR implementation for rational
 *          resampling of an input signal.
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

#include <filter/polyphase_fir.h>
#include <filter/polyphase_fir_priv.h>
#include <filter/filter.h>
#include <filter/filter_priv.h>
#include <filter/sample_buf.h>
#include <filter/utils.h>

#include <tsl/safe_alloc.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>

/**
 * Construct a new polyphase FIR.
 *
 * \param pfir The new polyphase FIR state, returned by reference.
 * \param nr_coeffs The number of coefficients in the FIR
 * \param fir_coeff The real coefficients for the FIR. In Q.15 representation.
 * \param interpolate The factor to interpolate (upsample) by
 * \param decimate The factor to decimate (downsample) by
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t polyphase_fir_new(struct polyphase_fir **pfir, size_t nr_coeffs, const int16_t *fir_coeff,
            unsigned interpolate, unsigned decimate)
{
    aresult_t ret = A_OK;

    struct polyphase_fir *fir = NULL;
    unsigned phase_coeffs = 0;

    TSL_ASSERT_ARG(NULL != pfir);
    TSL_ASSERT_ARG(0 != nr_coeffs);
    TSL_ASSERT_ARG(NULL != fir_coeff);
    TSL_ASSERT_ARG(0 < interpolate);
    TSL_ASSERT_ARG(0 < decimate);

    if (FAILED(ret = TZAALLOC(fir, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    fir->nr_phase_filters = interpolate;
    fir->interpolation = interpolate;
    fir->decimation = decimate;

    /* Determine the number of coefficients in each phase */
    phase_coeffs = (nr_coeffs + interpolate - 1)/interpolate;

    /* Round up to nearest 4 */
    phase_coeffs = (phase_coeffs + 3) & ~(4-1);
    fir->nr_filter_coeffs = phase_coeffs;

    if (FAILED(ret = TACALLOC((void **)&fir->phase_filters, interpolate,  phase_coeffs * sizeof(sample_t), SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    /* Walk the input filter and set the coefficients in the appropriate filter phase */
    for (size_t i = 0; i < nr_coeffs; i++) {
        fir->phase_filters[(i % interpolate) * phase_coeffs + (i / interpolate)] = fir_coeff[i];
    }

    for (size_t i = 0; i < fir->nr_phase_filters; i++) {
        printf("\nPhase %4zu: ", i);
        for (size_t j = 0; j < fir->nr_filter_coeffs; j++) {
            printf("%6d ", fir->phase_filters[i * fir->nr_filter_coeffs + j]);
        }
    }
    printf("\n");

    /* Bing, and we're done */
    *pfir = fir;

done:
    if (FAILED(ret)) {
        if (NULL != fir) {
            TFREE(fir);
        }
    }
    return ret;
}

/**
 * Delete/clean up resources consumed by a polyphase FIR.
 *
 * \param pfir The polyphase FIR to clean up, passed by reference. Set to NULL on success.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t polyphase_fir_delete(struct polyphase_fir **pfir)
{
    aresult_t ret = A_OK;

    struct polyphase_fir *fir = NULL;

    TSL_ASSERT_PTR_BY_REF(pfir);

    fir = *pfir;

    if (NULL != fir->phase_filters) {
        TFREE(fir->phase_filters);
    }

    TFREE(fir);
    *pfir = NULL;

    return ret;
}

aresult_t polyphase_fir_push_sample_buf(struct polyphase_fir *fir, struct sample_buf *buf)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(NULL != buf);

    TSL_BUG_ON(fir->sb_active == buf);
    TSL_BUG_ON(fir->sb_next == buf);

    if (NULL == fir->sb_active) {
        fir->sb_active = buf;
        TSL_BUG_ON(NULL != fir->sb_next);
    } else {
        if (NULL == fir->sb_next) {
            fir->sb_next = buf;
        } else {
            ret = A_E_BUSY;
            goto done;
        }
    }

    fir->nr_samples += buf->nr_samples;

done:
    return ret;
}

aresult_t polyphase_fir_process(struct polyphase_fir *fir, int16_t *out_buf, size_t nr_out_samples,
        size_t *nr_out_samples_generated)
{
    aresult_t ret = A_OK;

    size_t phase_id = 0,
           nr_consumed = 0,
           nr_computed_samples = 0;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(NULL != out_buf);
    TSL_ASSERT_ARG(0 != nr_out_samples);
    TSL_ASSERT_ARG(NULL != nr_out_samples_generated);

    *nr_out_samples_generated = 0;

    if (NULL == fir->sb_active && NULL == fir->sb_next) {
        goto done;
    }

    phase_id = fir->last_phase;

    for (size_t i = 0; i < nr_out_samples && fir->nr_samples > fir->nr_filter_coeffs; i++) {
        size_t interp_phase = 0;
        TSL_BUG_ON(phase_id >= fir->nr_phase_filters);

        aresult_t filt_ret = dot_product_sample_buffers_real(
                fir->sb_active,
                fir->sb_next,
                fir->sample_offset,
                &fir->phase_filters[fir->nr_filter_coeffs * phase_id],
                fir->nr_filter_coeffs,
                &out_buf[i]);

        if (filt_ret == A_E_DONE) {
            *nr_out_samples_generated = i;
            goto done;
        } else if (FAILED(filt_ret)) {
            goto done;
        }

        nr_computed_samples++;

        /* Calculate the next phase to process */
        phase_id += fir->decimation;

        interp_phase = phase_id / fir->interpolation;
        phase_id = phase_id % fir->interpolation;
        nr_consumed += interp_phase;
        fir->nr_samples -= interp_phase;

        /* Check if we're going to need to update the active buffer */
        if (fir->sample_offset + interp_phase > fir->sb_active->nr_samples) {
            /* Retire the active buffer, shift next to active */
            size_t old_nr_samples = fir->sb_active->nr_samples;
            TSL_BUG_IF_FAILED(sample_buf_decref(fir->sb_active));
            fir->sb_active = fir->sb_next;
            fir->sb_next = NULL;
            fir->sample_offset = fir->sample_offset + interp_phase - old_nr_samples;
        } else {
            /* Continue walking the current buffer */
            fir->sample_offset += interp_phase;
        }

        fir->last_phase = phase_id;
    }

    *nr_out_samples_generated = nr_computed_samples;

done:
    return ret;
}

aresult_t polyphase_fir_can_process(struct polyphase_fir *fir, bool *pcan_process)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(NULL != pcan_process);

    /* The trick for this is to see if there are at least enough samples to run a single pass of the
     * FIR.
     */
    *pcan_process = fir->nr_samples >= fir->nr_filter_coeffs;

    return ret;
}

aresult_t polyphase_fir_full(struct polyphase_fir *fir, bool *pfull)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != fir);
    TSL_ASSERT_ARG_DEBUG(NULL != pfull);

    *pfull = (NULL != fir->sb_next);

    return ret;
}

