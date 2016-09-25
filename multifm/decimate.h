#pragma once

#include <tsl/result.h>

struct sample_buf;

/**
 * An integer complex decimator. This is really implemented as a decimating
 * boxcar filter. It's the way of the road...
 *
 * Note that a decimation window must be a power of two.
 */
struct decimate {
    /**
     * Current accumulator for the real value
     */
    int32_t re_val;

    /**
     * Current accumulator for the imaginary value
     */
    int32_t im_val;

    /**
     * Number of samples currently accumulated
     */
    unsigned nr_samples;

    /**
     * The shift size.
     */
    unsigned shift_size;
};

aresult_t decimate_init(struct decimate *decim, uint32_t scale_factor);
aresult_t decmiate_process(struct decimate *decim, struct sample_buf *in,
        struct sample_buf *out);


