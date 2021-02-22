#pragma once

#include <tsl/work_queue.h>
#include <tsl/list.h>
#include <tsl/worker_thread.h>

#include <filter/direct_fir.h>
#include <filter/dc_blocker.h>

#include <pthread.h>

#define LPF_OUTPUT_LEN              1024

struct polyphase_fir;
struct demod_base;

/**
 * Demodulator thread context
 */
struct demod_thread {
    /**
     * SPSC queue used to deliver work to this worker thread
     */
    struct work_queue wq CAL_CACHE_ALIGNED;

    /**
     * The FIR filter being applied by this thread (usually for baseband selection)
     */
    struct direct_fir fir;

    /**
     * The file descriptor for the output FIFO
     */
    int fifo_fd;

    /**
     * The file descriptor for dumping the filtered signal
     */
    int debug_signal_fd;

    /**
     * Mutex for the work queue. Always must be held while manipulating it.
     */
    pthread_mutex_t wq_mtx;

    /**
     * Condition variable to be signalled when there is work to be done. This
     * thread will wait on the condvar until signalled to wake up by the
     * sample producer.
     */
    pthread_cond_t wq_cv;

    /**
     * Demodulator worker thread state
     */
    struct worker_thread wthr;

    /**
     * Demodulator state
     */
    struct demod_base *demod;

    /**
     * Linked list node demodulator thread
     */
    struct list_entry dt_node;

    /**
     * Total number of samples demodulated
     */
    size_t total_nr_demod_samples;

    /**
     * Total number of PCM samples generated
     */
    size_t total_nr_pcm_samples;

    /**
     * Number of samples dropped on the floor
     */
    size_t nr_dropped_samples;

    /**
     * Number of FM signal samples available
     */
    size_t nr_fm_samples;

    /**
     * Filtered samples to be processed
     */
    int16_t filt_samp_buf[2 * LPF_OUTPUT_LEN];

    /**
     * Number of good PCM samples
     */
    size_t nr_pcm_samples;

    /**
     * Output demodulated sample buffer
     */
    int16_t out_buf[LPF_OUTPUT_LEN];

    /**
     * CSQ level in dBFS (0 = open squelch)
     */
    int8_t csq_level_dbfs;
};

aresult_t demod_thread_delete(struct demod_thread **pthr);

/**
 * Create a new demodulation thread.
 *
 * \param demod_gain The gain of the channelizing FIR, expressed in linear units.
 *
 */
aresult_t demod_thread_new(struct demod_thread **pthr, unsigned core_id,
        int32_t offset_hz, uint32_t samp_hz, const char *out_fifo, int decimation_factor,
        const double *lpf_taps, size_t lpf_nr_taps,
        const char *fir_debug_output,
        double channel_gain,
        int csq_level_dbfs);

