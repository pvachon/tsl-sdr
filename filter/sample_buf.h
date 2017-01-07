#pragma once

#include <filter/filter.h>

#include <tsl/cal.h>
#include <tsl/result.h>

#include <stdint.h>

struct sample_buf;

/**
 * The sample representation contained in the given sample buffer
 */
enum sample_type {
    /**
     * Unknown sample representation
     */
    UNKNOWN             = 0,

    /**
     * Samples are real unsigned 16-bit integers
     */
    REAL_UINT_16        = 1,

    /**
     * Samples are complex unsigned 16-bit integers
     */
    COMPLEX_UINT_16     = 2,

    /**
     * Samples are complex signed 16-bit integers
     */
    COMPLEX_INT_16      = 3,

    /**
     * Samples are real unsigned 32-bit integers
     */
    REAL_UINT_32        = 4,

    /**
     * Samples are complex unsigned 32-bit integers
     */
    COMPLEX_UINT_32     = 5,
};

typedef aresult_t (*sample_buf_release_func_t)(struct sample_buf *buf);

/**
 * A sample buffer. Represents a count of samples, with the specified
 * integer format.
 *
 * If samples are real, they are packed one after the other.
 *   RRRRRRRRRRRRRR... etc.
 *
 * However, for complex samples, I and Q are interleaved directly:
 *   IQIQIQIQIQIQIQ... etc.
 */
struct sample_buf {
    /**
     * Reference count for this sample buf. This is decremented atomically, and cannot
     * be incremented after being initially set. Sample buffers can be freed in any
     * context.
     */
    uint32_t refcount CAL_ALIGN(16);

    /**
     * The type of a sample in this buffer.
     */
    enum sample_type sample_type;

    /**
     * The number of samples in this sample buffer
     */
    uint32_t nr_samples;

    /**
     * The size of the sample buffer, in bytes
     */
    uint32_t sample_buf_bytes;

    /**
     * The start timestamp (in sample time) of this buffer
     */
    uint64_t start_time_ns;

    /**
     * Pointer to the function that will be called to release the sample buffer once the reference
     * count reaches 0.
     */
    sample_buf_release_func_t release;

    /**
     * Private state an application can attach to the sample buffer.
     */
    void *priv;

    /**
     * The actual data. This will need to be cast appropriately.
     */
    uint8_t data_buf[];
};

aresult_t sample_buf_decref(struct sample_buf *buf);

