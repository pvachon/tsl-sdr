#pragma once

#include <multifm/receiver.h>

struct airspy_device;
struct config;

/**
 * State for the RTL-SDR reader thread
 */
struct airspy_thread {
    /**
     * The receiver context associated with this thread
     */
    struct receiver rx;

    /**
     * The Airspy device we're capturing from
     */
    struct airspy_device *dev;

    /**
     * File descriptor to dump raw samples to
     */
    int dump_fd;

    /**
     * The number of buffers we had to discard
     */
    uint64_t dropped;
};

/**
 * Create a new
 */
aresult_t airspy_worker_thread_new(struct receiver **pthr, struct config *cfg);

