#include <pager/mueller_muller.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <string.h>
#include <math.h>

aresult_t mm_init(struct mueller_muller *mm, float kw, float km, float samples_per_bit, float error_min, float error_max)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != mm);

    /* Clear the memory */
    memset(mm, 0, sizeof(struct mueller_muller));

    mm->next_offset = 0.0;
    mm->m = mm->w = mm->ideal_step_size = samples_per_bit;
    mm->kw = kw;
    mm->km = km;
    mm->error_min = error_min;
    mm->error_max = error_max;
    mm->samples_per_bit = samples_per_bit;

#ifdef _MM_DEBUG
    mm->nr_samples = 0;
#endif

    return ret;
}

static
float _mm_get_sign(float v)
{
    return (float)(v > 0) - (float)(v < 0);
}

aresult_t mm_process(struct mueller_muller *mm, const int16_t *samples, size_t nr_samples, int16_t *decisions,
        size_t nr_decisions, size_t *pnr_decisions_out)
{
    aresult_t ret = A_OK;

    float cur_sample = 0.0f,
          nr_samples_f = 0.0f,
          w = 0.0f,
          m = 0.0f;
    size_t cur_decision = 0;

    TSL_ASSERT_ARG(NULL != mm);
    TSL_ASSERT_ARG(NULL != samples);
    TSL_ASSERT_ARG(NULL != decisions);
    TSL_ASSERT_ARG(NULL != pnr_decisions_out);

    cur_sample = mm->next_offset;
    nr_samples_f = (float)nr_samples;
    w = mm->w;
    m = mm->m;

#ifdef _MM_DEBUG
    DIAG("mm_process: nr_samples_f = %f, next_step_size = %f, cur_sample = %f", nr_samples_f, w, cur_sample);
#endif

    while (cur_sample < nr_samples_f) {
        float sample = samples[(size_t)(cur_sample + 0.5f)];
        float w_error = 0.0f;

        /* TODO: we might want to interpolate here */
        decisions[cur_decision] = sample;
        cur_decision++;

        TSL_BUG_ON(cur_decision > nr_decisions);

        /* Calculate the error for our PI loop */
        w_error = _mm_get_sign(mm->last_sample) * sample - _mm_get_sign(sample) * mm->last_sample;

        /* Determine the next sample to process */
        w += w_error * mm->kw;

#ifdef _MM_DEBUG
        fprintf(stdout, "%f, %f, %f;\n", (double)cur_sample + (double)mm->nr_samples, (double)sample, (double)w_error);
#endif

        /* Clamp if our error is becoming too big */
        if (mm->error_min > w) {
            w = mm->error_min;
        } else if (mm->error_max < w) {
            w = mm->error_max;
        }

        m += w + mm->km * sample;

        /* Calculate the offset of the next sample to be processed */
        cur_sample += floorf(m);

        m -= floorf(m);

        /* Store the sample we just processed */
        mm->last_sample = sample;
    }

#ifdef _MM_DEBUG
    mm->nr_samples += nr_samples;
#endif

    /* Store the offset of the next sample to be processed in the following buffer */
    mm->next_offset = cur_sample - nr_samples_f;
    mm->w = w;
    mm->m = m;

    *pnr_decisions_out = cur_decision;

    return ret;
}

