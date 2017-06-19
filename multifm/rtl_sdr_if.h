#pragma once

#include <multifm/receiver.h>

struct rtlsdr_dev;
struct config;

/**
 * State for the RTL-SDR reader thread
 */
struct rtl_sdr_thread {
    /**
     * The receiver context associated with this thread
     */
    struct receiver rx;

    /**
     * The RTL SDR device we're capturing from
     */
    struct rtlsdr_dev *dev;

    /**
     * If set, samples will be ignored
     */
    bool muted;

    /**
     * File descriptor to dump raw samples to
     */
    int dump_fd;
};

/**
 * Create a new
 */
aresult_t rtl_sdr_worker_thread_new(struct receiver **pthr, struct config *cfg);

