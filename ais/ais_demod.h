#pragma once

#include <tsl/result.h>

#include <stdbool.h>

struct ais_demod;

/**
 * Callback called whenever a packet has been received.
 *
 * \param demod The demodulator state
 * \param state The state passed to the demodulator on creation
 * \param packet The raw packet, packed as binary
 * \param packet_len The length of the raw packet, in bytes
 * \param fcs_valid Boolean value indicating if the FCS is valid or not
 */
typedef aresult_t (*ais_demod_on_message_callback_func_t)(struct ais_demod *demod, void *state, const uint8_t *packet, size_t packet_len, bool fcs_valid);

/**
 * Create a new AIS demodulator.
 *
 * \param pdemod The new demodulator state, returned by reference
 * \param state State passed to the callback
 * \param cb The callback to execute after receiving and unpacking the AIS message (& checking FCS)
 * \param freq The frequency this is listening on. In Hz.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t ais_demod_new(struct ais_demod **pdemod, void *state, ais_demod_on_message_callback_func_t cb, uint32_t freq);

/**
 * Delete a demodulator's state
 *
 * \param pdemod The demodulator state, passed by reference. Set to NULL on deletion.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t ais_demod_delete(struct ais_demod **pdemod);

/**
 * Process a buffer of PCM samples, and decode any AIS packets found.
 *
 * \param demod The demodulator state
 * \param samples The buffer of PCM samples, 16-bit real-valued
 * \param nr_samples The number of samples
 */
aresult_t ais_demod_on_pcm(struct ais_demod *demod, const int16_t *samples, size_t nr_samples);

