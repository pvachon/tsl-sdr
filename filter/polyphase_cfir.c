#include <filter/polyphase_cfir.h>
#include <filter/sample_buf.h>
#include <filter/utils.h>
#include <filter/complex.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/safe_alloc.h>

#include <math.h>
#include <complex.h>

struct polyphase_cfir {
    /**
     * Filter coefficients. These are stored as complex integers.
     *
     * Real(H(i)) = 2 * i
     * Imag(H(i)) = 2 * i + 1
     */
    int16_t *phases;

    /**
     * Number of phases
     */
    size_t nr_phases;

    /**
     * Length of each phase of the filter
     */
    size_t phase_len;

    /**
     * The current sample buffer we're processing
    r*/
    struct sample_buf *sb_active;

    /**
     * Offset of the next input sample in our current
     * sample buffer
     */
    size_t cur_sample_off;

    /**
     * The next sample buffer we're going to process
     */
    struct sample_buf *sb_next;

    /**
     * Number of available samples in the filter
     */
    size_t nr_samples;

    /**
     * Last phase processed
     */
    size_t last_phase;

    /**
     * Interpolation factor
     */
    size_t interpolation;

    /**
     * Decimation factor
     */
    size_t decimation;

    /**
     * The real part of the Q.15 rotation phase increment
     */
    int16_t rot_phase_incr_re;

    /**
     * The imaginary part of the Q.15 rotaiton phase increment
     */
    int16_t rot_phase_incr_im;

    /**
     * The real part of the Q.15 rotation factor to be applied to each sample
     */
    int16_t rot_phase_re;

    /**
     * The imaginary part of the Q.15 rotation factor to be applied to each sample
     */
    int16_t rot_phase_im;

    /**
     * The rotation counter.
     */
    unsigned rot_counter;
};

aresult_t polyphase_cfir_new(struct polyphase_cfir **pfir, size_t nr_coeffs, const int16_t *fir_complex_coeff,
        unsigned interpolation, unsigned decimation,
        bool derotate, uint32_t sampling_rate, int32_t freq_shift)
{
    aresult_t ret = A_OK;

    struct polyphase_cfir *fir = NULL;
    size_t phase_coeffs = 0;

    TSL_ASSERT_ARG(NULL != pfir);
    TSL_ASSERT_ARG(0 != nr_coeffs);
    TSL_ASSERT_ARG(NULL != fir_complex_coeff);
    TSL_ASSERT_ARG(0 != interpolation);
    TSL_ASSERT_ARG(0 != decimation);

    if (FAILED(ret = TZAALLOC(fir, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    fir->nr_phases = interpolation;

    /* Calculate the length of a phase, round to 4 */
    phase_coeffs = (nr_coeffs + interpolation - 1)/interpolation;
    fir->phase_len = (phase_coeffs + 3) & ~(4 - 1);

    /* Allocate the phases */
    if (FAILED(ret = TACALLOC((void **)&fir->phases, interpolation, fir->phase_len * sizeof(int16_t) * 2, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    /* Fill in the coefficients for each phase */
    for (size_t i = 0; i < nr_coeffs; i++) {
        size_t coeff_offs = (i % interpolation) * fir->phase_len + (i / interpolation);
        fir->phases[2 * coeff_offs] = fir_complex_coeff[2 * i];
        fir->phases[2 * coeff_offs + 1] = fir_complex_coeff[2 * i + 1];
    }

    fir->nr_samples = 0;
    fir->last_phase = 0;
    fir->interpolation = interpolation;
    fir->decimation = decimation;

    if (true == derotate) {
        double fwt0 = 2.0 * M_PI * (double)freq_shift / (double)sampling_rate,
               q15 = 1ll << Q_15_SHIFT;
        complex double derotate_incr = cexp(CMPLX(0, -fwt0 * (double)decimation));
        fir->rot_phase_incr_re = (int32_t)(creal(derotate_incr) * q15);
        fir->rot_phase_incr_im = (int32_t)(cimag(derotate_incr) * q15);
        fir->rot_phase_re = 1ul << Q_15_SHIFT;
        fir->rot_phase_im = 0;
        DIAG("Derotation factor: %f, %f (%08x, %08x -> %f, %f)", creal(derotate_incr), cimag(derotate_incr),
                fir->rot_phase_incr_re, fir->rot_phase_incr_im,
                (double)fir->rot_phase_incr_re/q15, (double)fir->rot_phase_incr_im/q15);
    }

    *pfir = fir;

done:
    if (FAILED(ret)) {
        if (NULL != fir) {
            if (NULL != fir->phases) {
                TFREE(fir->phases);
            }
            TFREE(fir);
        }
    }
    return ret;
}

aresult_t polyphase_cfir_delete(struct polyphase_cfir **pfir)
{
    aresult_t ret = A_OK;

    struct polyphase_cfir *fir = NULL;

    TSL_ASSERT_ARG(NULL != pfir);
    TSL_ASSERT_ARG(NULL != *pfir);

    fir = *pfir;

    if (NULL != fir->phases) {
        TFREE(fir->phases);
    }

    TFREE(fir);

    *pfir = NULL;

    return ret;
}

aresult_t polyphase_cfir_push_sample_buf(struct polyphase_cfir *fir, struct sample_buf *buf)
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

/**
 * Perform a phase derotation for the current sample.
 */
static
aresult_t _polyphase_cfir_apply_derotation(struct polyphase_cfir *fir, int32_t acc_re_in, int32_t acc_im_in,
        int32_t *acc_re_out, int32_t *acc_im_out)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != fir);
    TSL_ASSERT_ARG_DEBUG(NULL != acc_re_out);
    TSL_ASSERT_ARG_DEBUG(NULL != acc_im_out);

    /* Apply the phase derotation to the sample */
    cmul_q15_q30(acc_re_in, acc_im_in, fir->rot_phase_re, fir->rot_phase_im,
            acc_re_out, acc_im_out);

    /* Now add the phase rotation increment to the phase rotation for the next sample */
    cmul_q15_q15(fir->rot_phase_re, fir->rot_phase_im, fir->rot_phase_incr_re, fir->rot_phase_incr_im,
            &fir->rot_phase_re, &fir->rot_phase_im);

    fir->rot_counter++;

    return ret;
}

aresult_t polyphase_cfir_process(struct polyphase_cfir *fir, int16_t *out_buf, size_t nr_out_samples,
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

    for (size_t i = 0; i < nr_out_samples && fir->nr_samples > fir->phase_len; i++) {
        size_t interp_phase = 0;
        int16_t samp_re = 0,
                samp_im = 0;
#ifdef _TSL_DEBUG
        TSL_BUG_ON(phase_id >= fir->phase_len);
#endif

        aresult_t filt_ret = dot_product_sample_buffers_complex(
                fir->sb_active,
                fir->sb_next,
                fir->cur_sample_off,
                &fir->phases[2 * fir->phase_len * phase_id],
                fir->phase_len,
                &samp_re,
                &samp_im);

        /* Apply a phase (de)rotation, if appropriate */
        if (!(0 == fir->rot_phase_incr_re && 0 == fir->rot_phase_incr_im)) {
            int32_t ret_re = 0,
                    ret_im = 0;
            TSL_BUG_IF_FAILED(_polyphase_cfir_apply_derotation(fir, samp_re, samp_im, &ret_re, &ret_im));
            samp_re = round_q30_q15(ret_re);
            samp_im = round_q30_q15(ret_im);
        }

        out_buf[2 * i] = samp_re;
        out_buf[2 * i + 1] = samp_im;

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
        if (fir->cur_sample_off + interp_phase > fir->sb_active->nr_samples) {
            /* Retire the active buffer, shift next to active */
            size_t old_nr_samples = fir->sb_active->nr_samples;
            TSL_BUG_IF_FAILED(sample_buf_decref(fir->sb_active));
            fir->sb_active = fir->sb_next;
            fir->sb_next = NULL;
            fir->cur_sample_off = fir->cur_sample_off + interp_phase - old_nr_samples;
        } else {
            /* Continue walking the current buffer */
            fir->cur_sample_off += interp_phase;
        }

        fir->last_phase = phase_id;
    }

    *nr_out_samples_generated = nr_computed_samples;

done:
    return ret;
}

aresult_t polyphase_cfir_can_process(struct polyphase_cfir *fir, bool *pcan_process)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != fir);
    TSL_ASSERT_ARG(NULL != pcan_process);

    /* The trick for this is to see if there are at least enough samples to run a single pass of the
     * FIR.
     */
    *pcan_process = fir->nr_samples >= fir->phase_len;

    return ret;
}

aresult_t polyphase_cfir_full(struct polyphase_cfir *fir, bool *pfull)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != fir);
    TSL_ASSERT_ARG_DEBUG(NULL != pfull);

    *pfull = (NULL != fir->sb_next);

    return ret;
}

