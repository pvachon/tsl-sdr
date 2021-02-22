#include <multifm/fm_demod.h>
#include <multifm/demod_base.h>
#include <multifm/fast_atan2f.h>
#include <multifm/multifm.h>

#include <filter/filter.h>

#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/safe_alloc.h>

#include <math.h>

struct multifm_fm_demod {
    struct demod_base demod;
    int32_t last_fm_re;
    int32_t last_fm_im;
};

aresult_t multifm_fm_demod_init(struct demod_base **pdemod)
{
    aresult_t ret = A_OK;

    struct multifm_fm_demod *demod = NULL;

    TSL_ASSERT_ARG(NULL != pdemod);
    *pdemod = NULL;

    TSL_BUG_IF_FAILED(TZAALLOC(demod, SYS_CACHE_LINE_LENGTH));

    *pdemod = &demod->demod;

    return ret;
}

aresult_t multifm_fm_demod_process(struct demod_base *demod, int16_t *in_samples, size_t nr_in_samples,
        int16_t *out_samples, size_t *pnr_out_samples, size_t *pnr_out_bytes, int csq_level_dbfs)
{
    aresult_t ret = A_OK;

    struct multifm_fm_demod *dfm = NULL;
    static const float to_q15 = (float)(1 << Q_15_SHIFT);
    size_t nr_out_samples = 0;

    TSL_ASSERT_ARG(NULL != demod);
    TSL_ASSERT_ARG(NULL != in_samples);
    TSL_ASSERT_ARG(0 != nr_in_samples);
    TSL_ASSERT_ARG(NULL != out_samples);
    TSL_ASSERT_ARG(NULL != pnr_out_samples);

    dfm = BL_CONTAINER_OF(demod, struct multifm_fm_demod, demod);

    // Calculate average power of samples
    float sum_smp_rms = 0;
    for (size_t i = 0; i < nr_in_samples; i++) {
        // Calculate RMS of I & Q samples
        int32_t re_smp = in_samples[2 * i];
        int32_t im_smp = in_samples[2 * i + 1];
        float smp_rms = sqrt( ( pow(re_smp,2) + pow(im_smp,2) ) / 2.0 );
        sum_smp_rms += smp_rms;
    }
    float avg_smp_rms = sum_smp_rms / (float)nr_in_samples;
    // Convert to dBm
    float avg_smp_vrms = (avg_smp_rms + SMP_OFFSET) / SMP_SCALE;
    float avg_smp_wrms = pow(avg_smp_vrms,2)/50.0;
    float avg_smp_dBFS = 10*log10(avg_smp_wrms);
    // Debug print
    //MFM_MSG(SEV_INFO, "CSQ_DEBUG", "Average sample pwr: %.3f, %.3f dBFS, calc. from %d samples", avg_smp_rms, avg_smp_dBFS, (int)nr_in_samples);

    // Demod the samples if we're above the threshold, silence if not
    for (size_t i = 0; i < nr_in_samples; i++) {
        // Get next samples
        int32_t a_re =  in_samples[2 * i    ],
                a_im =  in_samples[2 * i + 1];
        // Check squelch and demod if above threshold or if we're in open squelch
        if ((avg_smp_dBFS >= csq_level_dbfs) || csq_level_dbfs == 0) {
            /* Get the complex conjugate of the prior sample, negating the phase term */
            int32_t b_re =  dfm->last_fm_re,
                    b_im = -dfm->last_fm_im;
            int32_t s_re = 0,
                    s_im = 0;

            /* Calculate the phase difference */
            s_re = a_re * b_re - a_im * b_im;
            s_im = a_re * b_im + a_im * b_re;

            /* Convert from cartesian coordinates to a phase angle */
            /* TODO: This needs to be made full-integer */
            float phi = fast_atan2f((float)s_im, (float)s_re);

            /* Scale by pi (since atan2 returns an angle in (-pi,pi]), convert back to Q.15 */
            float phi_scaled = (phi/M_PI) * to_q15;
            out_samples[nr_out_samples] = (int16_t)phi_scaled;
        }
        // Output silence if below threshold
        else {
            out_samples[nr_out_samples] = (int16_t)0;
            
        }
        // Do this regardless
        nr_out_samples++;
        /* Store the last sample processed */
        dfm->last_fm_re = a_re;
        dfm->last_fm_im = a_im;
    }

    *pnr_out_samples = nr_out_samples;
    *pnr_out_bytes = nr_out_samples * sizeof(int16_t);

    return ret;
}

aresult_t multifm_fm_demod_cleanup(struct demod_base **pdemod)
{
    aresult_t ret = A_OK;

    struct multifm_fm_demod *demod = NULL;

    TSL_ASSERT_ARG(NULL != pdemod);
    TSL_ASSERT_ARG(NULL != *pdemod);

    demod = BL_CONTAINER_OF(*pdemod, struct multifm_fm_demod, demod);

    TFREE(demod);

    *pdemod = NULL;

    return ret;
}

