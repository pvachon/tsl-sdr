#pragma once

#include <multifm/types.h>

#include <stdint.h>

/**
 * A contiguous ring buffer. Samples will be removed at the tail, then inserted at the head. The ring is
 * always full.
 *
 * The sample ring is always a power of two in length.
 */
struct sample_ring {
    /**
     * The tail sample ID
     */
    uint32_t tail_sample_num;

    /**
     * The head sample ID
     */
    uint32_t head_sample_num;

    /**
     * The size of the ring, in entries
     */
    uint32_t nr_entries;

    /**
     * The mask for selecting an entry. This will be shifted by the sample size to get the offset.
     */
    uint32_t entry_mask;

    /**
     * The samples in the ring
     */
    uint8_t data_buf[];
};

