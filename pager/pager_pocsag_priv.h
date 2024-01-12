#pragma once

#include <pager/pager_pocsag.h>
#include <pager/bch_code.h>

#define PAGER_POCSAG_BATCH_BITS         512
#define PAGER_POCSAG_SYNC_BITS          32

/**
 * POCSAG decoder state
 */
enum pager_pocsag_state {
    /**
     * Searching for a synchronization codeword
     */
    PAGER_POCSAG_STATE_SEARCH = 0,

    /**
     * Received sync word
     */
    PAGER_POCSAG_STATE_SYNCHRONIZED = 1,

    /**
     * Receiving a batch (after sync word)
     */
    PAGER_POCSAG_STATE_BATCH_RECEIVE = 2,

    /**
     * Search for a sync codeword at previously synchronized bit rate (batch finished).
     * Can transition to:
     *  - Synchronized
     *  - Search
     */
    PAGER_POCSAG_STATE_SEARCH_SYNCWORD = 3,
};

/**
 * Synchronization codeword, used to detect the baud rate
 */
#define POCSAG_SYNC_CODEWORD            0x7cd215d8ul

/**
 * Idle codeword, used to detect when words in a batch are to be ignored.
 * Post-BCH correction.
 */
#define POCSAG_IDLE_CODEWORD            0x6983915eu

#define POCSAG_PAGER_BASE_BAUD_RATE     38400
#define POCSAG_PAGER_BAUD_512_SAMPLES   (POCSAG_PAGER_BASE_BAUD_RATE/512)
#define POCSAG_PAGER_BAUD_1200_SAMPLES  (POCSAG_PAGER_BASE_BAUD_RATE/1200)
#define POCSAG_PAGER_BAUD_2400_SAMPLES  (POCSAG_PAGER_BASE_BAUD_RATE/2400)

#define POCSAG_PAGER_MAX_ALNUM_LEN      42
#define POCSAG_PAGER_MAX_NUM_LEN        75

enum pager_pocsag_message_type {
    PAGER_POCSAG_MESSAGE_TYPE_NONE = 0,
    PAGER_POCSAG_MESSAGE_TYPE_UNKNOWN = 1,
    PAGER_POCSAG_MESSAGE_TYPE_ALPHA = 2,
    PAGER_POCSAG_MESSAGE_TYPE_NUMERIC = 3,
};

/**
 * Current POCSAG message decoding
 */
struct pager_pocsag_message_decode {
    /**
     * The currently decoded message, as alphanumeric
     */
    char message_alpha[512];

    /**
     * The next message alphanumeric byte to be written
     */
    size_t next_byte_alpha;

    /**
     * Score for the alpha message
     */
    int score_alpha;

    /**
     * We've seen non-printables, so we need to penalize
     */
    bool seen_nonprint;

    /**
     * The current decoded message, as numeric
     */
    char message_numeric[512];

    /**
     * The next message numeric byte to be written
     */
    size_t next_byte_numeric;

    /**
     * The CAPcode this message is destined for
     */
    uint32_t cap_code;

    /**
     * Active data word, alphanumeric
     */
    uint32_t data_word_alpha;

    /**
     * Number of bits in the active data word
     */
    size_t data_word_alpha_valid_bits;

    /**
     * Active data word, numeric
     */
    uint32_t data_word_numeric;

    /**
     * Number of bits in the active data word, numeric
     */
    size_t data_word_numeric_valid_bits;

    /**
     * Function value recorded in address word
     */
    uint8_t function;

    /**
     * Set to true if this is an early termination
     */
    bool early_termination;

    /**
     * The message type
     */
    enum pager_pocsag_message_type msg_type;
};

/**
 * Processing state for detecting the baud rate of a burst of POCSAG
 */
struct pager_pocsag_baud_detect {
    /**
     * The number of samples per bit, also the number of words in the 
     */
    uint32_t samples_per_bit;

    /**
     * The baud rate
     */
    uint16_t baud_rate;

    /**
     * The current word being processed
     */
    uint32_t cur_word;

    /**
     * Number of samples in the eye that match
     */
    uint32_t nr_eye_matches;

    /**
     * Sync word eye detection
     */
    uint32_t eye_detect[];
};

/**
 * Batch word collection state
 */
struct pager_pocsag_batch {
    /**
     * The slice bit count
     */
    uint16_t cur_sample_skip;

    /**
     * The current batch. Filled in by the baud block when it has
     * found a synchronization word.
     */
    uint32_t current_batch[PAGER_POCSAG_BATCH_BITS/32];

    /**
     * The current word in the batch
     */
    uint16_t current_batch_word;

    /**
     * The next bit to be populated in the batch
     */
    uint16_t current_batch_word_bit;

    /**
     * Total bit count in this batch
     */
    uint16_t bit_count;
};

/**
 * Sync search state, for after receiving a batch
 */
struct pager_pocsag_sync_search {
    /**
     * The current skipped sample ID
     */
    uint16_t cur_sample_skip;

    /**
     * Number of sync bits captured
     */
    size_t nr_sync_bits;

    /**
     * Current estimated sync word
     */
    uint32_t sync_word;
};

/**
 * POCSAG processing state.
 */
struct pager_pocsag {
    /**
     * The rate to skip samples at
     */
    uint16_t sample_skip;

    /**
     * The current baud rate, if any
     */
    uint16_t baud_rate;

    /**
     * Whether or not we should skip the BCH code validation
     */
    bool skip_bch;

    /**
     * Callback for handling the arrival of numeric messages
     */
    pager_pocsag_on_numeric_msg_func_t on_numeric;

    /**
     * Callback for handling the arrival of alphanumeric messages
     */
    pager_pocsag_on_alpha_msg_func_t on_alpha;

    /**
     * Batch parsing/handling state.
     */
    struct pager_pocsag_batch batch;

    /**
     * Sync word search state.
     */
    struct pager_pocsag_sync_search sync;

    /**
     * State for decoding 512bps POCSAG
     */
    struct pager_pocsag_baud_detect *baud_512;

    /**
     * State for decoding 1200bps SuperPOCSAG
     */
    struct pager_pocsag_baud_detect *baud_1200;

    /**
     * State for decoding 2400bps SuperPOCSAG
     */
    struct pager_pocsag_baud_detect *baud_2400;

    /**
     * Message decoder state
     */
    struct pager_pocsag_message_decode decoder;

    /**
     * State for BCH(31, 21) code
     */
    struct bch_code *bch;

    /**
     * Current state of the wire protocol handling
     */
    enum pager_pocsag_state cur_state;

    /**
     * Frequency, in Hertz, of the center of this pager channel
     */
    uint32_t freq_hz;
};

