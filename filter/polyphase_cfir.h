#pragma once

#include <tsl/result.h>
#include <tsl/diag.h>

#include <stdbool.h>

struct polyphase_cfir;
struct sample_buf;

/**
 * Instantiate a new polyphase complex FIR decimation filter
 *
 * \param pfir The returned polyphase FIR state structure
 * \param nr_coeffs The number of coefficients
 * \param fir_complex_coeff The complex coefficients, H(t), where &Real(H(t)) = 2 * t, &Imag(H(t)) = 2 * t + 1
 * \param interpolation The factor we'll interpolate the input stream by
 * \param decimation The factor we're decimating the input stream by
 * \param derotate Whether or not to enable the phase derotator (you usually want this)
 * \param sampling_rate The rate that the input signal was sampled at
 * \param freq_shift The shift, in Hz, to convert the bandpass filter to baseband
 */
aresult_t polyphase_cfir_new(struct polyphase_cfir **pfir, size_t nr_coeffs, const int16_t *fir_complex_coeff,
        unsigned interpolation, unsigned decimation,
        bool derotate, uint32_t sampling_rate, int32_t freq_shift);


aresult_t polyphase_cfir_delete(struct polyphase_cfir **pfir);
aresult_t polyphase_cfir_push_sample_buf(struct polyphase_cfir *fir, struct sample_buf *buf);
aresult_t polyphase_cfir_process(struct polyphase_cfir *fir, int16_t *sample_buf, size_t nr_out_samples,
        size_t *nr_output_samples);
aresult_t polyphase_cfir_can_process(struct polyphase_cfir *fir, bool *pcan_process);
aresult_t polyphase_cfir_full(struct polyphase_cfir *fir, bool *pfull);

