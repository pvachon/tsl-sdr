#pragma once

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
     * Samples are real unsigned 32-bit integers
     */
    REAL_UINT_32        = 4,

    /**
     * Samples are complex unsigned 32-bit integers
     */
    COMPLEX_UINT_32     = 5,
};

