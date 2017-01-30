#pragma once

#include <filter/complex.h>

#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

#include <math.h>
#include <string.h>

/**
 * State for a simple DC blocker with a differentiator ahead of a leaky integrator.
 */
struct dc_blocker {
    /**
     * Filter pole coefficient for leaky integrator. In Q.15 representation.
     */
    int32_t p;

    /**
     * Prior input sample, for the differentiator. x[n-1]. In Q.30 representation.
     */
    int32_t x_n_1;

    /**
     * Prior output sample for feedback, in Q.15. y[n-1]
     */
    int32_t y_n_1;

    /**
     * Accumulator, contains error. In Q.30.
     */
    int32_t acc;
};

/**
 * Initialize a DC blocker to its starting state, with a pole placed as specified.
 *
 * \param blk Enough memory to store a DC blocker's state
 * \param pole The location of the pole for the integrator.
 *
 * \return A_OK on success, an error code otherwise
 */
static inline
aresult_t dc_blocker_init(struct dc_blocker *blk, double pole)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != blk);
    TSL_ASSERT_ARG(0.0 != fabs(pole));

    memset(blk, 0, sizeof(*blk));

    /* Convert the pole to a fixed point integer */
    blk->p = (int16_t)((1.0 - pole) * (double)(1 << Q_15_SHIFT));

    return ret;
}

/**
 * Apply a simple DC blocker, based on a combination of a differentiator and a
 * leaky integrator.
 *
 * \param blocker The DC blocker state
 * \param samples Samples to apply the DC blocker to
 * \param nr_samples The number of samples in the input buffer
 *
 * \return A_OK on success, an error code otherwise.
 */
static inline
aresult_t dc_blocker_apply(struct dc_blocker *blocker, int16_t *samples, size_t nr_samples)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != blocker);
    TSL_ASSERT_ARG(NULL != samples);
    TSL_ASSERT_ARG(0 != nr_samples);

    for (size_t i = 0; i < nr_samples; i++) {
        /* Only the error from the previous sample remains */
        blocker->acc -= blocker->x_n_1;
        /* Update the current sample to Q.30, store as previous */
        blocker->x_n_1 = samples[i] << Q_15_SHIFT;
        /* Accumulate the leaky integrator term */
        blocker->acc += blocker->x_n_1 - blocker->p * blocker->y_n_1;
        /* Convert the output to Q.15 from Q.30 */
        blocker->y_n_1 = blocker->acc >> Q_15_SHIFT;
        samples[i] = blocker->y_n_1;
    }

    return ret;
}

