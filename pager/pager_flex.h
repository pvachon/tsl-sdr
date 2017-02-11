#pragma once

#include <tsl/result.h>

#include <stdint.h>

struct pager_flex;

enum pager_flex_msg_type {
    PAGER_FLEX_UNKNOWN,
    PAGER_FLEX_ALPHANUMERIC,
    PAGER_FLEX_NUMERIC,
    PAGER_FLEX_TONE,
};

/**
 * Callback type. This is registered with each pager_flex, and is called whenever there is a message to process.
 *
 * Provides the callee with the baud rate, the phase ID, message type and the cap code and the message decoded as
 * ASCII.
 *
 * \ul If the message type is TONE, then the message_len is 0 and message_bytes is NULL
 * \ul If the message type is NUMERIC, the message_bytes is an ASCII representation of the numeric page
 * \ul If the message type is ALPHANUMERIC, the message_bytes is an ASCII representation of the message
 * \ul If the message type is UNKNOWN, then message_bytes is the post-BCH processed FLEX page words.
 */
typedef aresult_t (*pager_flex_on_message_cb_t)(struct pager_flex *flex, uint16_t baud, char phase, uint32_t cap_code, enum pager_flex_msg_type message_type, const char *message_bytes, size_t message_len);


/**
 * Create a new FLEX pager handler.
 *
 * \param pflex The new FLEX pager handler. Returned by reference.
 * \param freq_hz The frequency, in Hertz, of this channel. Used for record keeping mostly.
 * \param on_msg Callback hit by the FLEX pager handler whenever a finished message is ready to be processed.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_flex_new(struct pager_flex **pflex, uint32_t freq_hz, pager_flex_on_message_cb_t on_msg);

/**
 * Delete a FLEX pager handler.
 *
 * \param pflex The FLEX pager handler to delete. Passed by reference. Set to NULL when cleanup is finished.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_flex_delete(struct pager_flex **pflex);

/**
 * Push a block of PCM samples through the FLEX pager decoder. This will decode and demodulate/deliver data
 * as soon as enough data is available.
 *
 * \param flex The FLEX pager decoder state
 * \param pcm_samples PCM samples, in Q.15
 * \param nr_samples The number of samples to process in pcm_samples
 */
aresult_t pager_flex_on_pcm(struct pager_flex *flex, const int16_t *pcm_samples, size_t nr_samples);

