#pragma once

#include <tsl/result.h>

#include <stdint.h>

//#define _MM_DEBUG

/**
 * State for a Mueller-Muller clock recovery.
 *
 * This is a soft-decision block, but it does not interpolate the samples. Thus, the
 * caller must slice the ouputs per their requirements.
 *
 * TODO: make this fixed point. Using an int32_t is probably enough overkill for error
 * accumulation to not murder us.
 */
struct mueller_muller {
    /* Number of samples, per bit, based on the sample rate */
    float samples_per_bit;

    /* Omega gain for control loop */
    float kw;

    /* Mu gain for control loop */
    float km;

    float error_min;
    float error_max;

    /**
     * Omega
     */
    float w;

    /**
     * Mu
     */
    float m;

    /**
     * Offset of the next sample to process in the following block.
     */
    float next_offset;

    /**
     * Prior sample processed
     */
    float last_sample;

    /**
     * The step size, specified at initialization
     */
    float ideal_step_size;

#ifdef _MM_DEBUG
    /**
     * Total samples processed
     */
    uint64_t nr_samples;
#endif
};

/**
 * Initialize a new Mueller-Muller CLock Recovery instance
 *
 * \param mm Memory to initialize state within
 * \param kw Omega gain
 * \param km Mu gain
 * \param samples_per_bit The number of samples of the input per decision output
 * \param error_min The minimum range of error in the control loop before clamping
 * \param error_max The maximum range of error in the control loop before clamping
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t mm_init(struct mueller_muller *mm, float kp, float km, float samples_per_bit, float error_min, float error_max);

/**
 * Feed the next sample block to the Mueller-Muller Clock Recovery.
 * \param mm The Mueller-Muller Clock Recovery instance
 * \param samples Raw PCM samples to process.
 * \param nr_samples The number of samples
 * \param decisions Soft bit decisions, coming from Mueller-Muller
 * \param nr_decisions The maximum number of decisions the current buffer can hold
 * \param pnr_decisions_out The number of decisions made
 *
 * \return A_OK on success, an error code otherwise
 *
 * \note nr_decisions Needs to be at least (nr_samples/samples_per_bit) + 2 in length. Each
 *       call to mm_process should be able to process the entirety of the input sample
 *       buffer in a single shot.
 *
 * \note The state for the Mueller-Muller Clock Recovery block will track the offset to the
 *       next sample in the subsequent sample buffer.
 */
aresult_t mm_process(struct mueller_muller *mm, const int16_t *samples, size_t nr_samples, int16_t *decisions,
        size_t nr_decisions, size_t *pnr_decisions_out);
