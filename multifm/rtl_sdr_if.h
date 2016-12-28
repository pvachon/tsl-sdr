#pragma once

#include <tsl/list.h>
#include <tsl/worker_thread.h>

struct frame_alloc;
struct rtlsdr_dev;
struct config;

/**
 * State for the RTL-SDR reader thread
 */
struct rtl_sdr_thread {
    /**
     * The worker thread that we're doing RTL-SDR acquisition in
     */
    struct worker_thread wthr;

    /**
     * The RTL SDR device we're capturing from
     */
    struct rtlsdr_dev *dev;

    /**
     * The frame allocator for sample buffers
     */
    struct frame_alloc *samp_alloc;

    /**
     * If set, samples will be ignored
     */
    bool muted;

    /**
     * The number of demodulator worker threads we're dispatching to. Used to
     * set the initial reference count
     */
    unsigned nr_demod_threads;

    /**
     * The list of all demodulator threads
     */
    struct list_entry demod_threads;

    /**
     * File descriptor to dump raw samples to
     */
    int dump_fd;
};

aresult_t rtl_sdr_worker_thread_new(
        struct config *cfg,
        uint32_t center_freq,
        uint32_t sample_rate,
        struct frame_alloc *samp_buf_alloc,
        struct rtl_sdr_thread **pthr);

void rtl_sdr_worker_thread_delete(struct rtl_sdr_thread **pthr);

