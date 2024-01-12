#pragma once

#include <tsl/result.h>
#include <stdbool.h>

struct pager_pocsag;

typedef aresult_t (*pager_pocsag_on_numeric_msg_func_t)(
        struct pager_pocsag *pocsag,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len,
        uint8_t function,
	uint32_t freq_hz);

typedef aresult_t (*pager_pocsag_on_alpha_msg_func_t)(
        struct pager_pocsag *pocsag,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len,
        uint8_t function,
	uint32_t freq_hz);

/**
 * Create a new POCSAG decoder.
 *
 * \param ppocsag The new POCSAG pager decoder, returned by reference.
 * \param freq_hz The center frequency of this channel
 * \param on_numeric Function called when a numeric page has been successfully decoded.
 * \param on_alpha Function called when an alphanumeric page has been successfully decoded.
 * \param skip_bch_decode Skip BCH checks (not recommended)
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_pocsag_new(struct pager_pocsag **ppocsag, uint32_t freq_hz, pager_pocsag_on_numeric_msg_func_t on_numeric,
        pager_pocsag_on_alpha_msg_func_t on_alpha, bool skip_bch_decode);

/**
 * Destroy a POCSAG decoder.
 *
 * \param ppocsag The decoder, passed by reference. Set to NULL on success.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_pocsag_delete(struct pager_pocsag **ppocsag);

/**
 * Process a block of PCM samples that have arrived, decoding any POCSAG messages contained within.
 *
 * \param pocsag The POCSAG decoder state.
 * \param pcm_samples the real-valued PCM samples, in Q.15 representation.
 * \param nr_samples The number of samples to process.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_pocsag_on_pcm(struct pager_pocsag *pocsag, const int16_t *pcm_samples, size_t nr_samples);

