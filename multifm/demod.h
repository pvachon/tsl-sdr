#pragma once

#include <tsl/list.h>

/**
 * Context for the narrow-band FM demodulator.
 */
struct demod_context {
    /**
     * The relative center frequency of this demodulator.
     */
    uint32_t f_c;

    /**
     * The low-pass filter/decimator filter, used to bring the baseband signal to sanity after shifting
     */
    struct lpf_decim filt;

    /**
     * File descriptor for output pipe
     */
    int out_fd;

    /**
     * Linked list node for tracking demodulation contexts (and scheduling them)
     */
    struct list_entry dc_node;
};

