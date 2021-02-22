#pragma once

#include <tsl/result.h>

/**
 *  divisor to convert raw sample power to microwatts for dBm calculation,
 *  found experimentally using a known channel power measured via SDRSharp.
 *  There's probably a much better way to calculated dBFS but this works "well enough," 
 *  and dbFS is a pretty relative measurement to begin with.
 */
#define SMP_SCALE   2300.0
#define SMP_OFFSET  -4.1

struct demod_base;

/**
 * FM Demodulator
 *
 * This phase discriminator FM demodulator will convert an input complex FM signal to
 * a real-valued PCM stream of samples. The FM demodulator infrastructure takes complex
 * 16-bit integer pairs as inputs, and outputs a single PCM integer value.
 */

/**
 * Initialize a new FM demodulator
 *
 * \param pdemod The demodulator state, returned by reference.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t multifm_fm_demod_init(struct demod_base **pdemod);

/**
 * Given the demodulator state, process the specified sample buffers, and write the output samples
 * out to the real-valued PCM buffer.
 */
aresult_t multifm_fm_demod_process(struct demod_base *demod, int16_t *in_samples, size_t nr_in_samples,
        int16_t *out_samples, size_t *pnr_out_samples, size_t *pnr_out_bytes, int csq_level_dbfs);

/**
 * Cleanup the resources used by the FM demodulator
 */
aresult_t multifm_fm_demod_cleanup(struct demod_base **pdemod);

