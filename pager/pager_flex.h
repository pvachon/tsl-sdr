#pragma once

#include <tsl/result.h>

#include <stdint.h>
#include <stdbool.h>

struct pager_flex;

/**
 * Callback type. This is registered with each pager_flex, and is called whenever there is an alphanumeric page to process.
 *
 * Provides the callee with the baud rate, cycle and frame number, the phase ID, message type and the cap code and the message
 * decoded as ASCII.
 */
typedef aresult_t (*pager_flex_on_alnum_msg_func_t)(
        struct pager_flex *flex,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        bool fragmented,
        bool maildrop,
        uint8_t seq_num,
        const char *message_bytes,
        size_t message_len);

/**
 * Callback type. This is registered with each pager_flex, and is called whenever there is a long numeric page to process.
 *
 * Provides the callee with the baud rate, cycle and frame number, the phase ID, message type and the cap code and the message
 * decoded as ASCII.
 */
typedef aresult_t (*pager_flex_on_num_msg_func_t)(
        struct pager_flex *flex,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        const char *message_bytes,
        size_t message_len);

/**
 * Create a new FLEX pager handler.
 *
 * \param pflex The new FLEX pager handler. Returned by reference.
 * \param freq_hz The frequency, in Hertz, of this channel. Used for record keeping mostly.
 * \param on_aln_msg Callback hit by the FLEX pager handler whenever a finished alphanumeric message is ready.
 * \param on_num_msg Callback hit by the FLEX pager handler whenever a finished numeric message is ready.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t pager_flex_new(struct pager_flex **pflex, uint32_t freq_hz, pager_flex_on_alnum_msg_func_t on_aln_msg,
        pager_flex_on_num_msg_func_t on_num_msg);

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

