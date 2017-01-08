/*
 *  utils.c - Reusable utilities for writing filters
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

#include <filter/utils.h>
#include <filter/filter_priv.h>
#include <filter/sample_buf.h>
#include <filter/complex.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

/**
 * Compute the dot product of samples spread across a zero-copy buffer with a coefficient vector.
 *
 * All inputs and outputs from this function are real-valued (i.e. baseband).
 *
 * \param sb_active The current sample buffer. Must never be NULL.
 * \param sb_next The "next" sample buffer. Must not be NULL if sb_active has fewer than nr_coeffs samples in it.
 * \param buf_start_offset The start offset, in samples, from the start of sb_active to the sample to be dotted starting from.
 * \param coeffs The coefficients to be dotted with samples in sb_active, sb_next. All real-valued.
 * \param nr_coeffs The number of coefficients in the coeffs vector.
 * \param psample The resultant sample. Returned by reference.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t dot_product_sample_buffers_real(
        struct sample_buf *sb_active,
        struct sample_buf *sb_next,
        size_t buf_start_offset,
        int16_t *coeffs,
        size_t nr_coeffs,
        int16_t *psample)
{
    aresult_t ret = A_OK;

    int32_t acc_res = 0;
    size_t coeffs_remain = 0,
           buf_offset = 0;
    struct sample_buf *cur_buf = NULL;

    TSL_ASSERT_ARG_DEBUG(NULL != sb_active);
    TSL_ASSERT_ARG_DEBUG(NULL != coeffs);
    TSL_ASSERT_ARG_DEBUG(0 != nr_coeffs);
    TSL_ASSERT_ARG_DEBUG(NULL != psample);

    coeffs_remain = nr_coeffs;
    cur_buf = sb_active;
    buf_offset = buf_start_offset;

    /* Check if we have enough samples available
     * TODO: we should check if there are enough samples in the following buffer, too.
     */
    if (buf_offset + nr_coeffs > sb_active->nr_samples && sb_next == NULL) {
        ret = A_E_DONE;
        goto done;
    }

    /* Walk the number of samples in the current buffer up to the filter size */
    do {
        /* Figure out how many samples to pull out */
        size_t nr_samples_in = cur_buf->nr_samples - buf_offset,
               start_coeff = nr_coeffs - coeffs_remain;

        /* Snap to either the number of coefficients in the FIR or the number of remaining
         * coefficients, whichever is smaller.
         */
        nr_samples_in = BL_MIN2(nr_samples_in, coeffs_remain);

        for (size_t i = 0; i < nr_samples_in; i++) {
#ifdef _TSL_DEBUG
            TSL_BUG_ON(i + start_coeff >= nr_coeffs);
            TSL_BUG_ON(i + buf_offset >= cur_buf->nr_samples);
#endif /* defined(_TSL_DEBUG) */

            int32_t sample = ((int16_t *)cur_buf->data_buf)[buf_offset + i],
                    coeff = coeffs[start_coeff + i];

            acc_res += sample * coeff;
        }

        /* If we iterate through, we'll start at the beginning of the next buffer */
        buf_offset = 0;
        coeffs_remain -= nr_samples_in;
#ifdef _TSL_DEBUG
        /* Check if we have coefficients remaining to process */
        TSL_BUG_ON(cur_buf == sb_next && coeffs_remain != 0);
#endif
        cur_buf = sb_next;
    } while (coeffs_remain != 0);

    /* Return the computed sample, in Q.15 (currently in Q.30 due to the prior multiplications) */
    *psample = round_q30_q15(acc_res);

done:
    return ret;
}

