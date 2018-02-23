#include <multifm/costas_demod.h>
#include <multifm/demod_base.h>

#include <filter/filter.h>

#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/safe_alloc.h>

#include <complex.h>
#include <math.h>

struct multifm_costas_demod {
    struct demod_base demod;
    float f_shift;
    float alpha;
    float beta;
    float last_phase;
    float f_dev;
    float f_dev_max;
    float f_dev_min;
    float e_max;
};

aresult_t multifm_costas_demod_init(struct demod_base **pdemod, float f_shift, float alpha, float beta,
        int16_t e_max)
{
    aresult_t ret = A_OK;

    struct multifm_costas_demod *demod = NULL;
    static const float to_q15 = (float)(1 << Q_15_SHIFT);

    TSL_ASSERT_ARG(NULL != pdemod);

    TSL_BUG_IF_FAILED(TZAALLOC(demod, SYS_CACHE_LINE_LENGTH));

    demod->f_shift = f_shift;
    demod->alpha = alpha;
    demod->beta = beta;

    demod->last_phase = 0.0f;
    demod->e_max = (float)e_max/to_q15;

    demod->f_dev = 2.0f * M_PI * f_shift;

    /* TODO: un-fix this error coefficient */
    demod->f_dev_max = demod->f_dev + 0.3f;
    demod->f_dev_min = demod->f_dev - 0.3f;

    *pdemod = &demod->demod;

    return ret;
}

aresult_t multifm_costas_demod_process(struct demod_base *demod, int16_t *in_samples, size_t nr_in_samples,
        int16_t *out_samples, size_t *pnr_out_samples, size_t *pnr_out_bytes)
{
    aresult_t ret = A_OK;

    static const float to_q15 = (float)(1 << Q_15_SHIFT);
    struct multifm_costas_demod *dc = NULL;
    float e_max = 0.0;

    TSL_ASSERT_ARG(NULL != demod);
    TSL_ASSERT_ARG(NULL != in_samples);
    TSL_ASSERT_ARG(0 != nr_in_samples);
    TSL_ASSERT_ARG(NULL != out_samples);
    TSL_ASSERT_ARG(NULL != pnr_out_samples);
    TSL_ASSERT_ARG(NULL != pnr_out_bytes);

    dc = BL_CONTAINER_OF(demod, struct multifm_costas_demod, demod);

    e_max = dc->e_max;

    *pnr_out_samples = 0;
    *pnr_out_bytes = 0;

    for (size_t i = 0; i < nr_in_samples; i++) {
        /* Grab the sample */
        float error = 0.0,
              phase = 0.0;
        complex float samp = CMPLX((float)in_samples[2 * i    ]/to_q15,
                                   (float)in_samples[2 * i + 1]/to_q15);
        complex float nco = cexpf(CMPLX(0, -dc->last_phase));
        complex float out_samp = samp * nco;

        error = cimagf(out_samp) * crealf(out_samp);

        if (error > e_max) error = e_max;
        else if (error < -e_max) error = -e_max;

        dc->f_dev = dc->f_dev + dc->beta * error;
        phase = dc->last_phase + dc->f_dev + dc->alpha * error;

        if (dc->f_dev > dc->f_dev_max) {
            dc->f_dev = dc->f_dev_max;
        } else if (dc->f_dev < dc->f_dev_min) {
            dc->f_dev = dc->f_dev_min;
        }

        dc->last_phase = fmodf(phase, 2 * M_PI);

        TSL_BUG_ON(fabsf(crealf(out_samp)) > 1.0);
        TSL_BUG_ON(fabsf(cimagf(out_samp)) > 1.0);

        out_samples[2 * i    ] = crealf(out_samp) * to_q15;
        out_samples[2 * i + 1] = cimagf(out_samp) * to_q15;
    }

    *pnr_out_samples = nr_in_samples;
    *pnr_out_bytes = nr_in_samples * 2 * sizeof(int16_t);

    return ret;
}

aresult_t multifm_costas_demod_cleanup(struct demod_base **pdemod)
{
    aresult_t ret = A_OK;

    struct multifm_costas_demod *demod = NULL;

    TSL_ASSERT_ARG(NULL != pdemod);
    TSL_ASSERT_ARG(NULL != *pdemod);

    demod = BL_CONTAINER_OF(*pdemod, struct multifm_costas_demod, demod);

    TFREE(demod);

    *pdemod = NULL;

    return ret;
}

