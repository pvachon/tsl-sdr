#include <multifm/direct_fir.h>
#include <multifm/sambuf.h>

#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/safe_alloc.h>

#include <string.h>
#if 0
#include <math.h>
#endif

#define _DIRECT_FIR_IMPLEMENTATION

aresult_t direct_fir_init(struct direct_fir *fir, size_t nr_coeffs, int32_t *fir_real_coeff,
        int32_t *fir_imag_coeff, unsigned decimation_factor, struct demod_thread *dthr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(0 != nr_coeffs);
    TSL_ASSERT_ARG(NULL != fir_real_coeff);
    TSL_ASSERT_ARG(NULL != fir_imag_coeff);
    TSL_ASSERT_ARG(0 != decimation_factor);
    TSL_ASSERT_ARG(NULL != dthr);

    memset(fir, 0, sizeof(struct direct_fir));

    TSL_BUG_IF_FAILED(TACALLOC((void **)&fir->fir_real_coeff, nr_coeffs, sizeof(int32_t), 16));
    memcpy(fir->fir_real_coeff, fir_real_coeff, nr_coeffs * sizeof(int32_t));
    TSL_BUG_IF_FAILED(TACALLOC((void **)&fir->fir_imag_coeff, nr_coeffs, sizeof(int32_t), 16));
    memcpy(fir->fir_imag_coeff, fir_imag_coeff, nr_coeffs * sizeof(int32_t));

    fir->decimate_factor = decimation_factor;
    fir->dthr = dthr;
    fir->nr_coeffs = nr_coeffs;

    return ret;
}

aresult_t direct_fir_cleanup(struct direct_fir *fir)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);

    if (NULL != fir->fir_real_coeff) {
        TFREE(fir->fir_real_coeff);
    }

    if (NULL != fir->fir_imag_coeff) {
        TFREE(fir->fir_imag_coeff);
    }

    if (NULL != fir->sb_active) {
        sample_buf_decref(fir->dthr, fir->sb_active);
        fir->sb_active = NULL;
    }

    if (NULL != fir->sb_next) {
        sample_buf_decref(fir->dthr, fir->sb_next);
        fir->sb_next = NULL;
    }

    fir->dthr = NULL;
    fir->decimate_factor = 0;

    return ret;
}

aresult_t direct_fir_push_sample_buf(struct direct_fir *fir, struct sample_buf *buf)
{
    aresult_t ret = A_OK;

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

#ifdef _DIRECT_FIR_IMPLEMENTATION
static
aresult_t _direct_fir_process_sample(struct direct_fir *fir, int32_t *psample_real, int32_t *psample_imag)
{
    aresult_t ret = A_OK;

    int64_t acc_re = 0,
            acc_im = 0;
    size_t coeffs_remain = 0,
           buf_offset = 0;
    struct sample_buf *cur_buf = NULL;
#if 0
    int16_t s_min = INT16_MAX,
            s_max = INT16_MIN;
    double s_total = 0.0;
#endif

    TSL_ASSERT_ARG_DEBUG(NULL != fir);
    TSL_ASSERT_ARG_DEBUG(NULL != psample_real);
    TSL_ASSERT_ARG_DEBUG(NULL != psample_imag);

    coeffs_remain = fir->nr_coeffs;
    cur_buf = fir->sb_active;
    buf_offset = fir->sample_offset;

    /* Check if we have enough samples available */
    if (fir->sample_offset + fir->nr_coeffs > fir->sb_active->nr_samples &&
            fir->sb_next == NULL)
    {
        ret = A_E_DONE;
        goto done;
    }

    /* Walk the number of samples in the current buffer up to the filter size */
    do {
        /* Figure out how many samples to pull out */
        size_t nr_samples_in = cur_buf->nr_samples - buf_offset,
               start_coeff = fir->nr_coeffs - coeffs_remain;

        /* Snap to either the number of coefficients in the FIR or the number of remaining
         * coefficients, whichever is smaller.
         */
        nr_samples_in = BL_MIN2(nr_samples_in, coeffs_remain);

#if 0
        /* DEBUG: dump the current buffer state */
        if (nr_samples_in != fir->nr_coeffs) {
            DIAG("Samples in: %zu Buffer Base: %zu Start Coeff: %zu", nr_samples_in, buf_offset, start_coeff);
        }
#endif

        for (size_t i = 0; i < nr_samples_in; i++) {
            TSL_BUG_ON(i + start_coeff >= fir->nr_coeffs);
            TSL_BUG_ON(i + buf_offset >= cur_buf->nr_samples);

            int16_t *sample = &((int16_t *)cur_buf->data_buf)[2 * (buf_offset + i)];

            int16_t raw_samp_re = sample[0],
                    raw_samp_im = sample[1];

#if 0
            double s_mag = sqrt((double)raw_samp_re * (double)raw_samp_re + (double)raw_samp_im * (double)raw_samp_im);

            s_min = BL_MIN2(s_min, (int16_t)s_mag);
            s_max = BL_MAX2(s_max, (int16_t)s_mag);
            s_total += s_mag;
#endif

            int64_t s_re = ((int64_t)raw_samp_re) << 23,
                    s_im = ((int64_t)raw_samp_im) << 23,
                    c_re = fir->fir_real_coeff[i + start_coeff],
                    c_im = fir->fir_imag_coeff[i + start_coeff],
                    f_re = 0,
                    f_im = 0;

            /* Filter the sample */
            f_re = c_re * s_re - c_im * s_im;
            f_im = c_re * s_im + c_im * s_re;

            /* Accumulate the sample */
            acc_re += f_re;
            acc_im += f_im;
        }

        /* If we iterate through, we'll start at the beginning of the next buffer */
        buf_offset = 0;
        cur_buf = fir->sb_next;
        coeffs_remain -= nr_samples_in;
    } while (coeffs_remain != 0);

#if 0
    printf("S Stats: min: %d max: %d mean: %f o: (%zd, %zd) ~ (%f, %f)\n", (int)s_min, (int)s_max, s_total/fir->nr_coeffs,
          acc_re >> 54, acc_im >> 54, (double)acc_re/(double)(1ll << 54), (double)acc_im/(double)(1ll << 54));
#endif

    /* Check if the next sample will start in the following buffer; if so, move along */
    if (fir->sample_offset + fir->decimate_factor > fir->sb_active->nr_samples) {
        fir->sb_active = fir->sb_next;
        fir->sb_next = NULL;
        fir->sample_offset = (fir->sample_offset + fir->decimate_factor) - fir->sb_active->nr_samples;
    } else {
        fir->sample_offset += fir->decimate_factor;
    }

    fir->nr_samples -= fir->decimate_factor;

    *psample_real = acc_re >> 31;
    *psample_imag = acc_im >> 31;

done:
    return ret;
}
#endif

aresult_t direct_fir_process(struct direct_fir *fir, int32_t *out_buf, size_t nr_out_samples,
        size_t *nr_out_samples_generated)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(NULL != out_buf);
    TSL_ASSERT_ARG(0 != nr_out_samples);
    TSL_ASSERT_ARG(NULL != nr_out_samples_generated);

    TSL_BUG_ON(NULL == fir->fir_real_coeff);
    TSL_BUG_ON(NULL == fir->fir_imag_coeff);
    TSL_BUG_ON(0 == fir->nr_coeffs);

    *nr_out_samples_generated = 0;

    if (NULL == fir->sb_active && NULL == fir->sb_next) {
        goto done;
    }

    for (size_t i = 0; i < nr_out_samples; i++) {
        if (A_E_DONE == _direct_fir_process_sample(fir, &out_buf[2 * i], &out_buf[2 * i + 1])) {
            *nr_out_samples_generated = i;
            goto done;
        }
    }

    *nr_out_samples_generated = nr_out_samples;

done:
    return ret;
}

aresult_t direct_fir_can_process(struct direct_fir *fir, bool *pcan_process, size_t *pest_count)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(NULL != pcan_process);

    /* The trick for this is to see if there are at least enough samples to run a single pass of the
     * FIR.
     */
    *pcan_process = fir->nr_samples >= fir->nr_coeffs;

    if (NULL != pest_count) {
        *pest_count = fir->nr_samples/fir->nr_coeffs;
    }

    return ret;
}

aresult_t direct_fir_full(struct direct_fir *fir, bool *pfull)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != fir);
    TSL_ASSERT_ARG_DEBUG(NULL != pfull);

    *pfull = (NULL != fir->sb_next);

    return ret;
}

