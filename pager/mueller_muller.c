#include <pager/mueller_muller.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <string.h>

aresult_t mm_init(struct mueller_muller *mm, float kp, float samples_per_bit, float error_min, float error_max)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != mm);

    /* Clear the memory */
    memset(mm, 0, sizeof(struct mueller_muller));

    mm->next_offset = samples_per_bit;

    return ret;
}

static
float _mm_get_sign(float v)
{
    return (v > 0) - (v < 0);
}

aresult_t mm_process(struct mueller_muller *mm, const int16_t *samples, size_t nr_samples, int16_t *decisions,
        size_t nr_decisions, size_t *pnr_decisions_out)
{
    aresult_t ret = A_OK;

    float cur_sample = 0.0f,
          nr_samples_f = 0.0f;
    size_t cur_decision = 0;

    TSL_ASSERT_ARG(NULL != mm);
    TSL_ASSERT_ARG(NULL != samples);
    TSL_ASSERT_ARG(NULL != decisions);
    TSL_ASSERT_ARG(NULL != pnr_decisions_out);

    cur_sample = mm->next_offset;
    nr_samples_f = (float)nr_samples;

    while (cur_sample < nr_samples_f) {
        float sample = samples[(size_t)(cur_sample + 0.5f)];
        float error = 0.0f,
              next_step_size = 0.0f;

        /* TODO: we might want to interpolate here */
        decisions[cur_decision] = sample;
        cur_decision++;

        TSL_BUG_ON(cur_decision > nr_decisions);

        /* Calculate the error for our PI loop */
        error = _mm_get_sign(mm->last_sample) * sample - _mm_get_sign(sample) * mm->last_sample;

        /* Determine the next sample to processed */
        next_step_size = next_step_size + error * mm->kp;

        /* Clamp if our error is becoming too big */
        if (mm->error_min > next_step_size) {
            next_step_size = mm->error_min;
        } else if (mm->error_max < next_step_size) {
            next_step_size = mm->error_max;
        }

        /* Calculate the offset of the next sample to be processed */
        cur_sample = cur_sample + next_step_size;

        /* Store the sample we just processed */
        mm->last_sample = sample;
    }

    /* Store the offset of the next sample to be processed in the following buffer */
    mm->next_offset = cur_sample - (float)nr_samples;

    return ret;
}

