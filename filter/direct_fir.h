#pragma once

#include <tsl/result.h>

#include <stdbool.h>

struct sample_buf;

struct direct_fir {
    /**
     * Real coefficients. Must be aligned to int32_t's natural alignment.
     */
    int16_t *fir_real_coeff;

    /**
     * Imaginary coefficients. Must be aligned to int32_t's natural alignment.
     */
    int16_t *fir_imag_coeff;

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
     * The real part of the Q.15 rotation phase increment
     */
    int32_t rot_phase_incr_re;

    /**
     * The imaginary part of the Q.15 rotaiton phase increment
     */
    int32_t rot_phase_incr_im;

    /**
     * The real part of the Q.15 rotation factor to be applied to each sample
     */
    int32_t rot_phase_re;

    /**
     * The imaginary part of the Q.15 rotation factor to be applied to each sample
     */
    int32_t rot_phase_im;

    /**
     * The rotation counter.
     */
    unsigned rot_counter;
};

/**
 * Create a direct coefficient FIR, in Q.15. This function allocates memory.
 *
 * \param fir The FIR object. Pass a chunk of memory by reference.
 * \param nr_coeffs The number of coefficients in the FIR
 * \param fir_real_coeff The real coefficients for the FIR
 * \param fir_imag_coeff The imaginary coefficients for the FIR
 * \param decimation_factor The decimation factor to apply
 * \param derotate Set to `true` if you wish to apply a derotator. Useful if the filter will
 *                 shift a signal to baseband.
 * \param sampling_rate The sampling rate. Used to manage the phase derotator. Ignored if not
 *                      using the phase derotator.
 * \param freq_shift If the filter will be downshifting another signal to baseband, how much
 *                   is this shift by, in Hz. Ignored if not using the phase derotator.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t direct_fir_init(struct direct_fir *fir, size_t nr_coeffs, const int16_t *fir_real_coeff,
        const int16_t *fir_imag_coeff, unsigned decimation_factor,
        bool derotate, uint32_t sampling_rate, int32_t freq_shift);

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
aresult_t direct_fir_process(struct direct_fir *fir, int16_t *out_buf, size_t nr_out_samples,
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
