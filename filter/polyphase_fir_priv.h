#pragma once

#include <stdint.h>

struct sample_buf;

/**
 * The state for a polyphase FIR. A Polyphase FIR is stored as a set of smaller filters, depending on the interpolation/decimation
 * factors.
 *
 * All coefficients are stored as Q.15, and are real by definition. The FIR is restructured as coefficients grouped in the appropriate
 * phase.
 *
 * The FIR, by definition, will represent taking an f_in stream of real samples and changing the output sample rate to
 *      f_out = I/D * f_in,
 * Where I, D are fairly small and relatively prime.
 *
 * The FIR that is to be applied to the FIR should be a low-pass filter for MIN(f_out, f_in), as to avoid imaging/aliasing.
 */
struct polyphase_fir {
    /**
     * The phase filters themselves. There are nr_phase_filters (L) with nr_filter_coeffs (M) coefficients.
     *
     * The filters are stored as a 2-d array, where each i'th filter's j'th coefficient can be found by:
     *      (i*M + j)
     */
    int16_t *phase_filters;

    /**
     * The number of phase filters in this polyphase FIR
     */
    size_t nr_phase_filters;

    /**
     * The number of filter coefficients in each phase filter. Where the polyphase filter would have less
     * than nr_filter_coeffs by design, the remaining coefficients will be 0. Typically this will be sized
     * based on SIMD requirements or similar (i.e. for ARM NEON it will be divisible by 4).
     */
    size_t nr_filter_coeffs;

    /**
     * The last phase we processed
     */
    size_t last_phase;

    /**
     * The interpolation factor
     */
    unsigned int interpolation;

    /**
     * The decimation factor
     */
    unsigned int decimation;

    /**
     * The current sample buffer to process
     */
    struct sample_buf *sb_active;

    /**
     * The next sample buffer to process
     */
    struct sample_buf *sb_next;

    /**
     * The total number of samples contained in this polyphase FIR
     */
    size_t nr_samples;

    /**
     * The next sample to be processed, relative to the start of sb_active.
     */
    size_t sample_offset;
};

