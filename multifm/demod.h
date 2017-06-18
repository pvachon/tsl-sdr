#pragma once

#include <tsl/work_queue.h>
#include <tsl/list.h>
#include <tsl/worker_thread.h>

#include <filter/direct_fir.h>
#include <filter/dc_blocker.h>

#include <pthread.h>

#define LPF_PCM_OUTPUT_LEN              1024

struct polyphase_fir;

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
     * An optional polyphase resampler.
     */
    struct polyphase_fir *pfir;

    /**
     * State for the DC blocker. Not used if DC blocker is disabled.
     */
    struct dc_blocker dc_blk;

    /**
     * Whether or not we want the DC blocker enabled
     */
    bool block_dc;

    /**
     * The file descriptor for the output FIFO
     */
    int fifo_fd;

    /**
     * The file descriptor for dumping the filtered signal
     */
    int debug_signal_fd;

    /**
     * The last FM sample of the prior buffer, I sample
     */
    int32_t last_fm_re;

    /**
     * The last FM sample of the prior buffer, Q sample
     */
    int32_t last_fm_im;

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
     * Linked list node demodulator thread
     */
    struct list_entry dt_node;

    /**
     * Total number of FM samples processed
     */
    size_t total_nr_fm_samples;

    /**
     * Total number of PCM samples generated
     */
    size_t total_nr_pcm_samples;

    /**
     * Number of FM signal samples available
     */
    size_t nr_fm_samples;

    /**
     * FM samples to be processed
     */
    int16_t fm_samp_out_buf[2 * LPF_PCM_OUTPUT_LEN];

    /**
     * Number of good PCM samples
     */
    size_t nr_pcm_samples;

    /**
     * Output PCM buffer
     */
    int16_t pcm_out_buf[LPF_PCM_OUTPUT_LEN];
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
        unsigned resample_decimate, unsigned resample_interpolate, const double *resample_filter_taps,
        size_t nr_resample_filter_taps,
        const char *fir_debug_output,
        double dc_block_pole, bool enable_dc_block,
        double channel_gain);

float fast_atan2f(float y, float x);

