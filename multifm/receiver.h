#pragma once

#include <tsl/result.h>
#include <tsl/worker_thread.h>
#include <tsl/list.h>

struct frame_alloc;
struct receiver;
struct config;
struct sample_buf;

typedef aresult_t (*receiver_cleanup_func_t)(struct receiver *rx);
typedef aresult_t (*receiver_rx_thread_func_t)(struct receiver *rx);

/**
 * Structure representing the generic state for a receiver. Usually embedded in a specialized
 * receiver structure.
 */
struct receiver {
    /**
     * Whether or not this thread is muted, to prevent output
     */
    bool muted;

    /**
     * Linked list of all demodulator threads
     */
    struct list_entry demod_threads;

    /**
     * The number of demodulator threads
     */
    size_t nr_demod_threads;

    /**
     * Frame allocator of sample buffers
     */
    struct frame_alloc *samp_alloc;

    /**
     * The worker thread for this receiver. Mandatory, each receiver must live in
     * its own separate worker thread apartment.
     */
    struct worker_thread wthr;

    /**
     * Function called to clean up the receiver state
     */
    receiver_cleanup_func_t cleanup_func;

    /**
     * Function called to kick of the rx thread actions for this receiver
     */
    receiver_rx_thread_func_t thread_func;
};

/**
 * Initialize a receiver structure, hooking the receiver into the multifm
 * framework. Call `receiver_start` to kick off the receiver thread. This
 * assumes receiver is already initialized with memory allocated by the driver.
 *
 * \param rx The receiver structure. Preallocated, usually embedded in a specific
 *           receiver type.
 * \param cfg The configuration for the overall receiver. Includes demodulation
 *            and filter thread configuration.
 * \param rx_func The receive function, called on starting the receive thread
 * \param cleanup_func The cleanup function, called when `receiver_cleanup()` is called.
 * \param samples_per_buf The number of samples for each buffer handled.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t receiver_init(struct receiver *rx, struct config *cfg,
        receiver_rx_thread_func_t rx_func, receiver_cleanup_func_t cleanup_func,
        size_t samples_per_buf);

/**
 * Start the receiver thread.
 */
aresult_t receiver_start(struct receiver *rx);

/**
 * Cleanup any hidden allocations in the receiver structure, and tear-down the
 * demodulation threads. Also terminates the receiver thread, and forces the 
 * driver to clean itself up.
 *
 * \param prx The receiver state. Passed by reference, set to NULL on success.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t receiver_cleanup(struct receiver **prx);

/**
 * Allocate a receiver sample buffer
 */
aresult_t receiver_sample_buf_alloc(struct receiver *rx, struct sample_buf **pbuf);

/**
 * Unmute/Mute the receiver
 */
aresult_t receiver_set_mute(struct receiver *rx, bool mute);

/**
 * Deliver a filled sample buffer to the receiver's listeners.
 *
 * \param rx The receiver state
 * \param buf The buffer of samples to be delivered.
 */
aresult_t receiver_sample_buf_deliver(struct receiver *rx, struct sample_buf *buf);

/**
 * Check if this receiver is still scheduled to be running
 *
 * \param rx The receiver state
 *
 * \return true if the thread is still scheduled to run, false otherwise
 *
 * \note asserts failure if rx is invalid
 */
bool receiver_thread_running(struct receiver *rx);

