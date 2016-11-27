#pragma once

#include <tsl/result.h>

#include <stdbool.h>

struct sample_buf;

struct direct_fir {
    /**
     * Real coefficients. Must be aligned to int32_t's natural alignment.
     */
    int32_t *fir_real_coeff;

    /**
     * Imaginary coefficients. Must be aligned to int32_t's natural alignment.
     */
    int32_t *fir_imag_coeff;

    /**
     * The number of coefficients in this FIR
     */
    size_t nr_coeffs;

    /**
     * Decimation factor. Determines how we walk through the sample buffer.
     */
    unsigned decimate_factor;

    /**
     * The offset of the next sample to be processed, in sb_active
     */
    unsigned sample_offset;

    /**
     * The total, pre-decimation number of samples available in the input buffer.
     */
    size_t nr_samples;

    /**
     * Active sample buffer being processed
     */
    struct sample_buf *sb_active;

    /**
     * Next sample buffer to be processed, if it's available
     */
    struct sample_buf *sb_next;

    /**
     * The thread state that this FIR is attached to
     */
    struct demod_thread *dthr;
};

/**
 * Create a direct coefficient FIR, in Q32. This function allocates memory.
 *
 * \param fir The FIR object. Pass a chunk of memory by reference.
 * \param nr_coeffs The number of coefficients in the FIR
 * \param fir_real_coeff The real coefficients for the FIR
 * \param fir_imag_coeff The imaginary coefficients for the FIR
 * \param decimation_factor The decimation factor to apply
 * \param dthr The demodulator thread that has the sample buffer allocator handle.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t direct_fir_init(struct direct_fir *fir, size_t nr_coeffs, int32_t *fir_real_coeff,
        int32_t *fir_imag_coeff, unsigned decimation_factor, struct demod_thread *dthr);

/**
 * Cleanup memory and release sample buffers for the FIR
 *
 * \param fir The FIR to cleanup.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t direct_fir_cleanup(struct direct_fir *fir);

/**
 * Push an updated sample buffer.
 *
 * \param fir The direct FIR to process
 * \param buf The buffer to push onto the queue
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t direct_fir_push_sample_buf(struct direct_fir *fir, struct sample_buf *buf);

/**
 * Apply the FIR to as many samples as possible, constrained by:
 * 1. The number of samples available (must be at least the FIR width of samples)
 * 2. The number of output buffer samples
 *
 * \param fir The FIR to apply
 * \param out_buf The buffer to write the output samples to
 * \param nr_out_samples The maximum number of output samples out_buf can hold
 * \param nr_output_samples_generated The number of valid samples in out_buf
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t direct_fir_process(struct direct_fir *fir, int32_t *out_buf, size_t nr_out_samples,
        size_t *nr_output_samples_generated);

/**
 * Determine whether or not an additional sample buffer can be passed to the FIR, for further
 * processing.
 *
 * \param fir The FIR in question
 * \param pfull Whether or not the FIR has space for an extra sample buffer, returned by reference.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t direct_fir_full(struct direct_fir *fir, bool *pfull);

/**
 * Determine whether or not there are enough samples available to produce at least one filtered,
 * decimated sample.
 *
 * \param fir The FIR in question
 * \param pfull Whether or not the FIR has enough samples available to produce one filtered sample.
 * \param pest_count The estimated count of samples that could be produced.
 */
aresult_t direct_fir_can_process(struct direct_fir *fir, bool *pcan_process, size_t *pest_count);
