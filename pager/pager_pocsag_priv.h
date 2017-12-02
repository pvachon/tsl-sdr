#pragma once

#include <pager/mueller_muller.h>

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
    PAGER_POCSAG_SYNCHRONIZED = 1,

    /**
     * Receiving a batch (after first codeword)
     */
    PAGER_POCSAG_BATCH_RECEIVE = 2,

    /**
     * Search for a sync codeword (batch finished).
     * Can transition to:
     *  - Synchronized
     *  - Search
     */
    PAGER_POCSAG_SEARCH_CODEWORD = 3,
};

/**
 * Synchronization codeword, used to detect the baud rate
 */
#define POCSAG_SYNC_CODEWORD        0x7cd215d8ul

/**
 * Idle codeword, used to detect when words in a batch are to be ignored
 */
#define POCSAG_IDLE_CODEWORD        0x7a89c197ul

/**
 * Processing state for a single POCSAG baud
 */
struct pager_pocsag_baud {
    /**
     * Mueller-Muller CDR for the given frequency
     */
    struct mueller_muller mm;

    /**
     * Synchronization check word for this baud rate
     */
    uint32_t sync_check;
};

/**
 * POCSAG processing state.
 */
struct pager_pocsag {

};

