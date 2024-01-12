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
        size_t message_len,
	uint32_t freq_hz);

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
        size_t message_len,
	uint32_t freq_hz);

/**
 * Special Instruction Vector (SIV) types
 * \{
 */

/**
 * Temporary Address Activation, for the specified pagers (by CAPCODE)
 */
#define PAGER_FLEX_SIV_TEMP_ADDRESS_ACTIVATION              0x0

/**
 * System Event, used to signal faults and system state changes.
 */
#define PAGER_FLEX_SIV_SYSTEM_EVENT                         0x1

/**
 * A reserved test vector type.
 */
#define PAGER_FLEX_SIV_RESERVED_TEST                        0x3

/**
 *\}
 */

/**
 * Callback type. This is registered with each pager_flex, and is called whenever there is a short instruction
 * vector to process.
 *
 * Provides the callee with information about the FLEX stream, as well as the SIV type.
 */
typedef aresult_t (*pager_flex_on_siv_msg_func_t)(
        struct pager_flex *flex,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        uint8_t siv_msg_type,
        uint32_t data,
	uint32_t freq_hz);

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
        pager_flex_on_num_msg_func_t on_num_msg, pager_flex_on_siv_msg_func_t on_siv_msg);

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

