#pragma once

#include <stdbool.h>

struct pager_flex;
struct bch_code;

enum pager_flex_modulation {
    PAGER_FLEX_MODULATION_2FSK,
    PAGER_FLEX_MODULATION_4FSK
};

enum pager_flex_state {
    /**
     * We're hunting for a sync pattern for Sync 1. This includes searching for
     * the alternating 1/0 pattern, while also monitoring for any of the 16-bit
     * Sync A codes.
     *
     * This uses the alternating 1/0 pattern at 1600 bps to increase confidence
     * of a sync match. The 1/0 pattern is not a discriminating pattern.
     */
    PAGER_FLEX_STATE_SYNC_1,

    /**
     * We've found the sync pattern, verified it, eaten the FIW, and are now entering
     * the second Sync phase. This is where we train the 4FSK slicer.
     */
    PAGER_FLEX_STATE_SYNC_2,

    /**
     * We're now decoding blocks of this frame
     */
    PAGER_FLEX_STATE_BLOCK,
};

enum pager_flex_sync_state {
    /**
     * Searching for the Bitsync 1 pattern
     */
    PAGER_FLEX_SYNC_STATE_SEARCH_BS1,

    /**
     * Found the Bitsync 1 pattern, 32-bits long
     */
    PAGER_FLEX_SYNC_STATE_BS1,

    /**
     * Looking for the A word of the sync. 32-bits long, 16-bits indicating the state, 16-bits being constant
     */
    PAGER_FLEX_SYNC_STATE_A,

    /**
     * Looking for the B word (not strict) - 16 bits
     */
    PAGER_FLEX_SYNC_STATE_B,

    /**
     * Looking for the A mode word, inverted - 32-bits
     */
    PAGER_FLEX_SYNC_STATE_INV_A,

    /**
     * Accumulating the FIW
     */
    PAGER_FLEX_SYNC_STATE_FIW,

    /**
     * That's it. Once we have the FIW, we check all the state pieces. If they check out, we can expose
     * state to the FLEX pager object itself. If they don't we reset to BS1 state. The FLEX pager object
     * will then switch to Sync 2 if everything checks out.
     */
    PAGER_FLEX_SYNC_STATE_SYNCED,
};

struct pager_flex_coding;

/**
 * FLEX Sync 1 stage state tracker. Tracks the detection of various sync phases in Sync 1,
 * then stores the current state for the rest of the objects to extract.
 */
struct pager_flex_sync {
    uint32_t sync_words[10];
    enum pager_flex_sync_state state;
    uint8_t sample_counter;
    uint8_t bit_counter;
    uint32_t a;
    uint16_t b;
    uint32_t inv_a;
    uint32_t fiw;
    struct pager_flex_coding *coding;

    /**
     * Sum of samples in sequence, high.
     */
    int32_t range_avg_sum_high;

    /**
     * Sum of samples in sequence, low
     */
    int32_t range_avg_sum_low;

    /**
     * 
     */
    unsigned range_avg_count_high;

    /**
     *
     */
    unsigned range_avg_count_low;
};

enum pager_flex_sync_2_state {
    /**
     * Accumulate comma values. We calculate the envelope of the signal during this period.
     */
    PAGER_FLEX_SYNC_2_STATE_COMMA,

    /**
     * Accumulate the C pattern
     */
    PAGER_FLEX_SYNC_2_STATE_C,

    /**
     * Accumulate the inverted comma
     */
    PAGER_FLEX_SYNC_2_STATE_INV_COMMA,

    /**
     * Accumulate inverted C
     */
    PAGER_FLEX_SYNC_2_STATE_INV_C,

    /**
     * We're close enough on the C pattern now, let's start handling the block.
     */
    PAGER_FLEX_SYNC_2_STATE_SYNCED,
};

/**
 * FLEX Sync 2 stage state tracker. Tracks the comma and the C pattern.
 *
 * This also detects the envelope of the signal, to train the 4FSK slicer.
 */
struct pager_flex_sync_2 {
    /**
     * Current state of Sync 2 decoding
     */
    enum pager_flex_sync_2_state state;

    /**
     * The count of the number of dots we've seen
     */
    uint16_t nr_dots;

    /**
     * The C value we've accumulated (diagnostic only)
     */
    uint16_t c;

    /**
     * The inverse C value we've accumulated (diagnostic only)
     */
    uint16_t inv_c;

    /**
     * Number of bits of C we've processed
     */
    uint8_t nr_c;
};

/**
 * Maximum number of words in a phase.
 */
#define PAGER_FLEX_PHASE_WORDS          88

/**
 * A phase of FLEX data. Each phase contains 88 words, across 11 blocks.
 */
struct pager_flex_phase {
    /**
     * Raw words, de-interleaved, for this phase
     */
    uint32_t phase_words[PAGER_FLEX_PHASE_WORDS];

    /**
     * Current bit within the current word
     */
    uint8_t cur_bit;

    /**
     * The current word being filled in
     */
    uint8_t cur_word;

    /**
     * The base word, to determine within phase_words where cur_word is relative to.
     */
    uint8_t base_word;
};

#define PAGER_FLEX_PHASE_A              0
#define PAGER_FLEX_PHASE_B              1
#define PAGER_FLEX_PHASE_C              2
#define PAGER_FLEX_PHASE_D              3
#define PAGER_FLEX_PHASE_MAX            4

/**
 * State for the block accumulation state
 */
struct pager_flex_block {
    /**
     * The individual data phases
     */
    struct pager_flex_phase phase[PAGER_FLEX_PHASE_MAX];

    /**
     * The number of symbols accumulated
     */
    int32_t nr_symbols;

    /**
     * The current phase flip/flop
     */
    bool phase_ff;
};

/**
 * A FLEX pager decoder.
 *
 * The input for this must always be a 16kHz signal.
 */
struct pager_flex {
    /**
     * Range from minimum to maximum sample value
     */
    int16_t sample_range;

    /**
     * Difference to subtract from each sample
     */
    int16_t sample_delta;

    /**
     * Callback hit on a complete alphanumeric message
     */
    pager_flex_on_alnum_msg_func_t on_alnum_msg;

    /**
     * Callback hit on a complete numeric message
     */
    pager_flex_on_num_msg_func_t on_num_msg;

    /**
     * Callback hit on a Special Instruction Vector message
     */
    pager_flex_on_siv_msg_func_t on_siv_msg;

    /**
     * Synchronization state for the FLEX message stream
     */
    struct pager_flex_sync sync;

    /**
     * State for the second phase of the synchronization.
     */
    struct pager_flex_sync_2 sync_2;

    /**
     * The block acquisition phase state
     */
    struct pager_flex_block block;

    /**
     * State for the BCH Error Corrector for the BCH(31, 23) code FLEX uses
     */
    struct bch_code *bch;

    /**
     * The current state of the FLEX receiver
     */
    enum pager_flex_state state;

    /**
     * The number of samples to skip before sampling for slicing
     */
    int16_t skip;

    /**
     * The skip count
     */
    int16_t skip_count;

    /**
     * The symbol sample rate. The number of samples that represents a single symbol
     */
    uint16_t symbol_samples;

    /**
     * Frequency, in Hertz, of the center of this pager channel
     */
    uint32_t freq_hz;

    /**
     * The current cycle number
     */
    uint8_t cycle_id;

    /**
     * The current frame number
     */
    uint8_t frame_id;

    /**
     * Message buffer (for ASCII decoding message bodies)
     */
    char msg_buf[256];

    /**
     * Current length of the valid message in the message buffer.
     */
    size_t msg_len;
};

/**
 * Slice function using the FLEX pager state.
 */
typedef int8_t (*pager_flex_slice_sym_func_t)(struct pager_flex *flex, int16_t sample);

struct pager_flex_coding {
    /**
     * The identifier A-code sequence
     */
    uint16_t seq_a;

    /**
     * The baud rate
     */
    uint16_t baud;

    /**
     * The number of FSK levels this coding presents
     */
    uint8_t fsk_levels;

    /**
     * The number of samples we'll skip when we shift to Sync 2 and beyond.
     */
    uint8_t sample_skip;

    /**
     * Number of samples the Sync 2 phase has of the standard 0xa sequence
     */
    uint8_t sync_2_samples;

    /**
     * Number of bits in a symbol
     */
    uint8_t sym_bits;

    /**
     * Fudge factor applied when performing the sample rate transition.
     */
    uint8_t sample_fudge;

    /**
     * Number of symbols in the data block
     */
    uint16_t symbols_per_block;

    /**
     * Number of phases
     */
    uint8_t nr_phases;

    /**
     * Slicer function
     */
    pager_flex_slice_sym_func_t slice;
};

/**
 * Extract various fields from the Frame Information Word
 *@{
 */
#define PAGER_FLEX_FIW_CHKSUM(x)                ((x) & 0xf)
#define PAGER_FLEX_FIW_CYCLE(x)                 (((x) >> 4) & 0xf)
#define PAGER_FLEX_FIW_FRAME(x)                 (((x) >> 8) & 0x7f)
#define PAGER_FLEX_FIW_ROAMING(x)               (!!((x) & (1 << 15)))
#define PAGER_FLEX_FIW_REPEAT(x)                (!!((x) & (1 << 16)))
/**
 *@}
 */

/**
 * Various odds and ends of FLEX frame sync constants. These are used for the SYNC 1 phase
 *
 * Any FLEX frame SYNC 1 phase is structured as follows:
 * - 32-bits of 0xaaaaaaaa
 * - 16-bit A sync code leader (taken from _pager_codings)
 * - 16-bit A sync fixed magic (0x5939)
 * - 16-bit B sync fixed magic (0x5555)
 * - 16-bit A sync code leader, inverted
 * - 16-bit A sync fixed magic, inverted
 *
 * Immediately following the sync sequence is the Frame Info Word (FIW). This word is
 * protected using a BCH(31,7) code (same used for data words).
 *
 * SYNC1 is always transmitted as 1600bps, 2FSK.
 * @{
 */

/**
 * The BS1 pattern
 */
#define PAGER_FLEX_SYNC_BS1                 0xaaaaaaaaul

/**
 * Value always present in 'A' binary pattern in SYNC 1
 */
#define PAGER_FLEX_SYNC_MAGIC_A             0x5939ul

/**
 * Value always present after the intial A sync magic
 */
#define PAGER_FLEX_SYNC_MAGIC_B             0x5555ul

/**
 * Sync 2 is performed at the speed specified in the magic of Sync 1. This phase allows the
 * receiver to synchronize itself with the transmitter at the target transmission speed.
 *
 * This phase is a variable number of bits long, depending on the sync speed.
 *
 * The phase always has 2 sync phases -- BS2/C, and inv BS2/C. The latter is simply transmitted
 * as an inverse of the former.
 *
 * In our implementation, we use this phase to train the slicer for 4FSK. If the encoding is
 * 2 FSK, we'll continue to use a binary slicer.
 */

#define PAGER_FLEX_SYNC_2_MAGIC_C           0xed84

/**
 * End of Sync Constants
 * @}
 */

/**
 * Vector Type Codes
 *
 * These type codes define how the message block should be interpreted.
 * @{
 */

#define PAGER_FLEX_MESSAGE_SECURE                   0x0
#define PAGER_FLEX_MESSAGE_SPECIAL_INSTRUCTION      0x1
#define PAGER_FLEX_MESSAGE_TONE                     0x2
#define PAGER_FLEX_MESSAGE_STANDARD_NUMERIC         0x3
#define PAGER_FLEX_MESSAGE_SPECIAL_NUMERIC          0x4
#define PAGER_FLEX_MESSAGE_ALPHANUMERIC             0x5
#define PAGER_FLEX_MESSAGE_HEX                      0x6
#define PAGER_FLEX_MESSAGE_NUMBERED_NUMERIC         0x7

/**
 * End of Vector Type Codes
 * @}
 */

/**
 * Short Message/Tone Only Codes
 * @{
 */
#define PAGER_FLEX_SHORT_TYPE_3_OR_8                0x0
#define PAGER_FLEX_SHORT_TYPE_8_SOURCES             0x1
#define PAGER_FLEX_SHORT_TYPE_SOURCES_AND_NUM       0x2
#define PAGER_FLEX_SHORT_TYPE_UNUSED                0x3
/**
 * End of Short Message/Tone Only Codes
 * @}
 */

