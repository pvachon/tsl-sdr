#include <multifm/multifm.h>
#include <multifm/sambuf.h>
#include <multifm/direct_fir.h>

#include <config/engine.h>

#include <app/app.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/safe_alloc.h>
#include <tsl/errors.h>
#include <tsl/worker_thread.h>
#include <tsl/frame_alloc.h>
#include <tsl/work_queue.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <complex.h>
#include <pthread.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <rtl-sdr.h>

#ifdef _USE_ARM_NEON
#include <arm_neon.h>
#endif

#define RTL_SDR_DEFAULT_NR_SAMPLES      (16 * 32 * 512/2)
#define LPF_PCM_OUTPUT_LEN              1024
#define Q_15_SHIFT                      14

/**
 * Sample data type
 */
typedef int32_t sample_t;

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
     * Raw sample buffer allocator
     */
    struct frame_alloc *samp_buf_alloc;

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

aresult_t sample_buf_decref(struct demod_thread *thr, struct sample_buf *buf)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != thr);
    TSL_ASSERT_ARG(NULL != buf);

    /* Decrement the reference count */
    if (1 == atomic_fetch_sub(&buf->refcount, 1)) {
        DIAG("Freeing buffer at %p", buf);
        TSL_BUG_IF_FAILED(frame_free(thr->samp_buf_alloc, (void **)&buf));
    }

    return ret;
}

static
aresult_t demod_thread_process(struct demod_thread *dthr, struct sample_buf *sbuf)
{
    aresult_t ret = A_OK;

    bool can_process = false;

    TSL_ASSERT_ARG(NULL != dthr);
    TSL_ASSERT_ARG(NULL != sbuf);

    DIAG("New Sample Buffer: %u samples.", sbuf->nr_samples);

    TSL_BUG_IF_FAILED(direct_fir_push_sample_buf(&dthr->fir, sbuf));
    TSL_BUG_IF_FAILED(direct_fir_can_process(&dthr->fir, &can_process, NULL));

    TSL_BUG_ON(false == can_process);

    while (true == can_process) {
        size_t nr_samples = 0;

        /* 1. Filter using FIR, decimate by the specified factor. Iterate over the output
         *    buffer samples.
         */
        TSL_BUG_IF_FAILED(direct_fir_process(&dthr->fir, dthr->fm_samp_out_buf + dthr->nr_fm_samples,
                    LPF_PCM_OUTPUT_LEN - dthr->nr_fm_samples, &nr_samples));

        DIAG("FIR Output Samples: %zu - %zu initially", nr_samples, dthr->nr_fm_samples);

        dthr->nr_fm_samples += nr_samples;
        dthr->total_nr_fm_samples += nr_samples;

        /* 2. Perform quadrature demod, write to output demodulation buffer. */
        /* TODO: smarten this up a lot - this sucks */
        dthr->nr_pcm_samples = 0;

        for (size_t i = 0; i < dthr->nr_fm_samples; i++) {
            TSL_BUG_ON(LPF_PCM_OUTPUT_LEN <= dthr->nr_pcm_samples);
            /* Get the complex conjugate of the prior sample - calculate the instantaneous phase difference between the two. */
            int32_t b_re =  dthr->last_fm_re,
                    b_im = -dthr->last_fm_im,
                    a_re =  dthr->fm_samp_out_buf[2 * i    ],
                    a_im =  dthr->fm_samp_out_buf[2 * i + 1];

            int32_t s_re = a_re * b_re - a_im * b_im,
                    s_im = a_im * b_re + a_re * b_im;

            /* XXX: todo: this needs to be made full-integer
             * Calculate the instantaneous phase difference */
            double sample = atan2((double)(s_im >> Q_15_SHIFT), (double)(s_re >> Q_15_SHIFT));
            dthr->pcm_out_buf[dthr->nr_pcm_samples] = (int16_t)(sample/M_PI * (double)(1ul << Q_15_SHIFT));

            dthr->nr_pcm_samples++;

            /* Store the last sample processed */
            dthr->last_fm_re = a_re;
            dthr->last_fm_im = a_im;
        }

        /* x. Write out the resulting PCM samples */
        if (0 > write(dthr->fifo_fd, dthr->pcm_out_buf, dthr->nr_pcm_samples * sizeof(int16_t))) {
            int errnum = errno;
            PANIC("Failed to write %zu bytes to the output fifo. Reason: %s (%d)", sizeof(int16_t) * dthr->nr_pcm_samples,
                    strerror(errnum), errnum);
        }

        TSL_BUG_IF_FAILED(direct_fir_can_process(&dthr->fir, &can_process, NULL));

        /* We're done with this batch of samples, woohoo */
        dthr->nr_fm_samples = 0;
    }

    /* Force the thread to wait until a new buffer is available */

    return ret;
}

static
aresult_t _demod_thread_work(struct worker_thread *wthr)
{
    aresult_t ret = A_OK;

    struct demod_thread *dthr = BL_CONTAINER_OF(wthr, struct demod_thread, wthr);

    pthread_mutex_lock(&dthr->wq_mtx);

    while (worker_thread_is_running(wthr)) {
        struct sample_buf *buf = NULL;
        TSL_BUG_IF_FAILED(work_queue_pop(&dthr->wq, (void **)&buf));

        if (NULL != buf) {
            pthread_mutex_unlock(&dthr->wq_mtx);

            /* Process the buffer */
            TSL_BUG_IF_FAILED(demod_thread_process(dthr, buf));

            /* Release the buffer reference */
            TSL_BUG_IF_FAILED(sample_buf_decref(dthr, buf));

            /* Re-acquire the lock */
            pthread_mutex_lock(&dthr->wq_mtx);
        } else {
            /* Wait until the acquisition thread wakes us up */
            int pt_en = 0;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            if (0 != (pt_en = pthread_cond_timedwait(&dthr->wq_cv, &dthr->wq_mtx, &ts))) {
                DIAG("Warning: nothing was ready for us to consume. %s (%d)", strerror(pt_en),
                        pt_en);
                continue;
            }
        }
    }

    DIAG("Processed %zu samples before termination.", dthr->total_nr_fm_samples);

    return ret;
}

static
aresult_t demod_thread_delete(struct demod_thread **pthr)
{
    aresult_t ret = A_OK;

    struct demod_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != *pthr);

    thr = *pthr;

    TSL_BUG_IF_FAILED(worker_thread_request_shutdown(&thr->wthr));
    TSL_BUG_IF_FAILED(worker_thread_delete(&thr->wthr));
    TSL_BUG_IF_FAILED(work_queue_release(&thr->wq));

    if (-1 != thr->fifo_fd) {
        close(thr->fifo_fd);
        thr->fifo_fd = -1;
    }

    direct_fir_cleanup(&thr->fir);

    TFREE(thr);

    *pthr = NULL;

    return ret;
}

/**
 * Prepare a FIR for channelizing. Converts tuned LPF to a band-pass filter.
 *
 * \param thr The thread to attach the FIR to
 * \param lpf_taps The taps for the direct-form FIR. These are real, the filter must be at baseband.
 * \param lpf_nr_taps The number of taps in the direct-form FIR. This is the order of the filter + 1.
 * \param offset_hz The offset, in hertz, from the center frequency
 * \param sample_rate The sample rate of the input stream
 * \param decimation The decimation factor for the output from this FIR.
 *
 * \return A_OK on success, an error code otherwise
 */
static
aresult_t _demod_fir_prepare(struct demod_thread *thr, double *lpf_taps, size_t lpf_nr_taps, int32_t offset_hz, uint32_t sample_rate, int decimation)
{
    aresult_t ret = A_OK;

    int16_t *coeffs = NULL;
    double f_offs = -2.0 * M_PI * (double)offset_hz / (double)sample_rate;
#ifdef _DUMP_LPF
    int64_t power = 0;
    double dpower = 0.0;
#endif /* defined(_DUMP_LPF) */
    size_t base = lpf_nr_taps;

    DIAG("Preparing LPF for offset %d Hz", offset_hz);

    TSL_ASSERT_ARG(NULL != thr);
    TSL_ASSERT_ARG(NULL != lpf_taps);
    TSL_ASSERT_ARG(0 != lpf_nr_taps);

    if (FAILED(ret = TACALLOC((void *)&coeffs, lpf_nr_taps, sizeof(int16_t) * 2, SYS_CACHE_LINE_LENGTH))) {
        MFM_MSG(SEV_FATAL, "NO-MEM", "Out of memory for FIR.");
        goto done;
    }

#ifdef _DUMP_LPF
    fprintf(stderr, "lpf_shifted_%d = [\n", offset_hz);
#endif /* defined(_DUMP_LPF) */

    for (size_t i = 0; i < lpf_nr_taps; i++) {
        /* Calculate the new tap coefficient */
        const double complex lpf_tap = cexp(CMPLX(0, f_offs * (double)i)) * lpf_taps[i];
        const double q15 = 1ll << Q_15_SHIFT;
#ifdef _DUMP_LPF
        double ptemp = 0;
        int64_t samp_power = 0;
#endif

        /* Calculate the Q31 coefficient */
        coeffs[       i] = (int16_t)(creal(lpf_tap) * q15 + 0.5);
        coeffs[base + i] = (int16_t)(cimag(lpf_tap) * q15 + 0.5);

#ifdef _DUMP_LPF
        ptemp = sqrt( (creal(lpf_tap) * creal(lpf_tap)) + (cimag(lpf_tap) * cimag(lpf_tap)) );
        samp_power = sqrt( ((int64_t)coeffs[i] * (int64_t)coeffs[i]) + ((int64_t)coeffs[base + i] * (int64_t)coeffs[base + i]) ) + 0.5;

        power += samp_power;
        dpower += ptemp;

        fprintf(stderr, "    complex(%f, %f), %% (%d, %d)\n", creal(lpf_tap), cimag(lpf_tap), coeffs[i], coeffs[base + i]);
#endif /* defined(_DUMP_LPF) */
    }
#ifdef _DUMP_LPF
    fprintf(stderr, "];\n");
    fprintf(stderr, "%% Total power: %llu (%016llx) (%f)\n", power, power, dpower);
#endif /* defined(_DUMP_LPF) */

    /* Create a Direct Type FIR implementation */
    TSL_BUG_IF_FAILED(direct_fir_init(&thr->fir, lpf_nr_taps, coeffs, &coeffs[base], decimation, thr, true, sample_rate, offset_hz));

done:
    if (NULL != coeffs) {
        TFREE(coeffs);
    }

    return ret;
}

static
aresult_t demod_thread_new(struct demod_thread **pthr, unsigned core_id, struct frame_alloc *samp_buf_alloc,
        int32_t offset_hz, uint32_t samp_hz, const char *out_fifo, int decimation_factor,
        double *lpf_taps, size_t lpf_nr_taps)
{
    aresult_t ret = A_OK;

    struct demod_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != samp_buf_alloc);
    TSL_ASSERT_ARG(NULL != out_fifo && '\0' != *out_fifo);
    TSL_ASSERT_ARG(0 != decimation_factor);
    TSL_ASSERT_ARG(NULL != lpf_taps);
    TSL_ASSERT_ARG(0 != lpf_nr_taps);

    *pthr = NULL;

    if (FAILED(ret = TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    thr->fifo_fd = -1;

    /* Initialize the work queue */
    if (FAILED(ret = work_queue_new(&thr->wq, 128))) {
        goto done;
    }

    /* Initialize the mutex */
    if (0 != pthread_mutex_init(&thr->wq_mtx, NULL)) {
        goto done;
    }

    /* Initialize the condition variable */
    if (0 != pthread_cond_init(&thr->wq_cv, NULL)) {
        goto done;
    }

    /* Initialize the filter */
    if (FAILED(ret = _demod_fir_prepare(thr, lpf_taps, lpf_nr_taps, offset_hz, samp_hz, decimation_factor))) {
        goto done;
    }

    /* Open the output FIFO */
    if (0 > (thr->fifo_fd = open(out_fifo, O_WRONLY))) {
        MFM_MSG(SEV_FATAL, "CANT-OPEN-FIFO", "Unable to open output fifo '%s'", out_fifo);
        goto done;
    }

    thr->samp_buf_alloc = samp_buf_alloc;

    list_init(&thr->dt_node);

    TSL_BUG_IF_FAILED(worker_thread_new(&thr->wthr, _demod_thread_work, core_id));

    *pthr = thr;

done:
    if (FAILED(ret)) {
        if (NULL != thr) {
            if (-1 != thr->fifo_fd) {
                close(thr->fifo_fd);
                thr->fifo_fd = -1;
            }

            TSL_BUG_IF_FAILED(direct_fir_cleanup(&thr->fir));

            TFREE(thr);
        }
    }
    return ret;
}

/**
 * RTL-SDR API Callback, hit every time there is a full sample buffer to be
 * processed.
 *
 * \param buf The buffer, as packed 8-bit I/Q unsigned values
 * \param len The length of the buffer, in number of samples
 * \param ctx Context structure. In this case, it's the RTL-SDR worker thread.
 *
 */
static
void __rtl_sdr_worker_read_async_cb(unsigned char *buf, uint32_t len, void *ctx)
{
    struct rtl_sdr_thread *thr = ctx;
    struct sample_buf *sbuf = NULL;
    int16_t *sbuf_ptr = NULL;
    struct demod_thread *dthr = NULL;

    if (true == thr->muted) {
        DIAG("Worker is muted.");
        /* If the receiver side is muted, there's no need to process this buffer */
        goto done;
    }

    if (0 <= thr->dump_fd) {
        if (0 > write(thr->dump_fd, buf, len)) {
            DIAG("Failed to write %u bytes to the RTL-SDR dump file.", len);
        }
    }

    /* Allocate an output buffer */
    if (FAILED(frame_alloc(thr->samp_alloc, (void **)&sbuf))) {
        MFM_MSG(SEV_INFO, "NO-SAMPLE-BUFFER", "Out of sample buffers.");
        goto done;
    }

    sbuf_ptr = (int16_t *)sbuf->data_buf;

    /* Up-convert the u8 samples to Q.15, subtract 127 from the unsigned sample to get actual power */
#ifdef _USE_ARM_NEON
    int16x8_t samples,
              sub_const  = { 127, 127, 172, 172, 127, 127, 127, 127 };
    uint8x8_t raw_samples;
    for (size_t i = 0; i < len/8; i++) {
        size_t offs = i * 8;

        __builtin_prefetch(buf + offs);

        /* Load as unsigned 8b */
        raw_samples = vld1_u8(buf + offs);

        /* Convert to s16 - we can get away with the reinterpret because all values are [0, 255] */
        samples = vreinterpretq_s16_u16(vmovl_u8(raw_samples));

        /* subtract 127 */
        samples = vsubq_s16(samples, sub_const);

        /* Shift left by 7 */
        samples = vqshlq_n_s16(samples, 7);

        /* Store in the output buffer at the appropriate location */
        vst1q_s16(sbuf_ptr + offs, samples);

#if 0
        printf("{%d, %d, %d, %d, %d, %d, %d, %d}\n",
                samples[0], samples[1], samples[2], samples[3], samples[4], samples[5], samples[6], samples[7]);
#endif
    }

    /* If there's a remainder because the sample count is not divisible by 8, process the remainder */
    size_t buf_offs = len & ~(8 - 1);

    for (size_t i = 0; i < len % 8; i++) {
        sbuf_ptr[i + buf_offs] = ((int16_t)buf[i + buf_offs] - 127) << 7;
    }

#else /* Works for any architecture */
    for (size_t i = 0; i < len; i++) {
        sbuf_ptr[i] = ((int16_t)buf[i] - 127) << 7;
    }
#endif

    sbuf->nr_samples = len / 2;
    atomic_store(&sbuf->refcount, thr->nr_demod_threads);

    /* TODO: Apply the DC filter.. maybe */

    /* Make it available to each demodulator/processing thread */
    list_for_each_type(dthr, &thr->demod_threads, dt_node) {
        pthread_mutex_lock(&dthr->wq_mtx);
        TSL_BUG_IF_FAILED(work_queue_push(&dthr->wq, sbuf));
        pthread_mutex_unlock(&dthr->wq_mtx);
        /* Signal there is data ready, if the thread is waiting on the condvar */
        pthread_cond_signal(&dthr->wq_cv);
    }

done:
    return;
}

static
aresult_t _rtl_sdr_worker_thread(struct worker_thread *wthr)
{
    aresult_t ret = A_OK;

    struct rtl_sdr_thread *thr = BL_CONTAINER_OF(wthr, struct rtl_sdr_thread, wthr);
    int rtl_ret = 0;

    DIAG("Starting RTL-SDR worker thread");

    /* We will turn control of this thread over to libusb/librtlsdr */
    if (0 != (rtl_ret = rtlsdr_read_async(thr->dev, __rtl_sdr_worker_read_async_cb, thr, 0, 0))) {
        MFM_MSG(SEV_WARNING, "UNCLEAN-TERM", "The RTL-SDR Async Reader terminated with an error (%d).", rtl_ret);
    }

    MFM_MSG(SEV_INFO, "RECEIVER-THREAD-TERMINATED", "Terminating RTL-SDR Receiver thread...");

    return ret;
}

static
aresult_t __rtl_sdr_worker_set_gain(struct rtlsdr_dev *dev, int gain)
{
    aresult_t ret = A_OK;

    void *gains CAL_CLEANUP(free_memory) = NULL;
    int nr_gains = 0,
        real_gain = 0;
    int *gain_n = NULL;

    TSL_ASSERT_ARG(NULL != dev);

    /* Disable AGC */
    if (0 != rtlsdr_set_agc_mode(dev, 0)) {
        MFM_MSG(SEV_WARNING, "CANT-SET-AGC", "Failed to disable AGC.");
    }

    if (0 >= (nr_gains = rtlsdr_get_tuner_gains(dev, NULL))) {
        MFM_MSG(SEV_ERROR, "CANT-GET-GAINS", "Unable to get list of supported gains.");
        ret = A_E_INVAL;
        goto done;
    }

    TSL_BUG_IF_FAILED(TCALLOC(&gains, sizeof(int), (size_t)nr_gains));

    if (0 >= rtlsdr_get_tuner_gains(dev, gains)) {
        MFM_MSG(SEV_ERROR, "CANT-GET-GAIN-LIST", "Unable to get list of gains.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Print list of supported gains, for shits and giggles */
    printf("Receiver supports %d gains: ", nr_gains);
    gain_n = gains;
    for (int i = 0; i < nr_gains; i++) {
        printf("%d.%d ", gain_n[i]/10, gain_n[i] % 10);
    }
    printf("\n");

    real_gain = gain_n[0];
    for (int i = 1; i < nr_gains; i++) {
        if (real_gain >= gain) {
            break;
        }
        real_gain = gain_n[i];
    }

    MFM_MSG(SEV_INFO, "RECV-GAIN", "Setting receive gain to %d.%d dB",
            real_gain / 10, real_gain % 10);

    if (0 > rtlsdr_set_tuner_gain_mode(dev, 1)) {
        MFM_MSG(SEV_ERROR, "FAILED-TO-ENABLE-MANUAL-GAIN", "Unable to enable manual gain, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    DIAG("Setting RTLSDR gain to %d", real_gain);

    if (0 != rtlsdr_set_tuner_gain(dev, real_gain)) {
        MFM_MSG(SEV_ERROR, "FAILED-SET-GAIN", "Failed to set requested gain.");
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

/**
 * Create and start a new worker thread for the RTL-SDR.
 */
static
aresult_t _rtl_sdr_worker_thread_new(
        struct config *cfg,
        uint32_t center_freq,
        uint32_t sample_rate,
        struct frame_alloc *samp_buf_alloc,
        struct rtl_sdr_thread **pthr)
{
    aresult_t ret = A_OK;

    struct config channel = CONFIG_INIT_EMPTY,
                  channels = CONFIG_INIT_EMPTY;
    struct rtl_sdr_thread *thr = NULL;
    struct rtlsdr_dev *dev = NULL;
    const char *rtl_dump_file = NULL;
    int dev_idx = -1,
        rret = 0,
        ppm_corr = 0,
        rtl_ret = 0,
        decimation_factor = 0,
        dump_file_fd = -1;
    size_t arr_ctr = 0,
           lpf_nr_taps = 0;
    bool test_mode = false;
    double *lpf_taps = NULL;
    double gain_db = 0.0;


    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != samp_buf_alloc);

    *pthr = NULL;

    /* Grab the decimation factor and other parameters first, just to validate them. */
    if (FAILED(ret = config_get_integer(cfg, &decimation_factor, "decimationFactor"))) {
        decimation_factor = 1;
        MFM_MSG(SEV_INFO, "NO-DECIMATION", "Not decimating the output signal: using full bandwidth.");
        ret = A_E_INVAL;
        goto done;
    }

    if (0 >= decimation_factor) {
        MFM_MSG(SEV_ERROR, "BAD-DECIMATION-FACTOR", "Decimation factor of '%d' is not valid.",
                decimation_factor);
        ret = A_E_INVAL;
        goto done;
    }

    /* Check that there's a filter specified */
    if (FAILED(ret = config_get_float_array(cfg, &lpf_taps, &lpf_nr_taps, "lpfTaps"))) {
        MFM_MSG(SEV_ERROR, "BAD-FILTER-TAPS", "Need to provide a baseband filter with at least two filter taps as 'lpfTaps'.");
        goto done;
    }

    if (1 >= lpf_nr_taps) {
        MFM_MSG(SEV_ERROR, "INSUFF-FILTER-TAPS", "Not enough filter taps for the low-pass filter.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Figure out which RTL-SDR device we want. */
    if (FAILED(ret = config_get_integer(cfg, &dev_idx, "deviceIndex"))) {
        MFM_MSG(SEV_ERROR, "NO-DEV-SPEC", "Need to specify a 'deviceIndex' entry in configuration");
        ret = A_E_INVAL;
        goto done;
    }

    if (0 != (rret = rtlsdr_open(&dev, dev_idx))) {
        MFM_MSG(SEV_ERROR, "BAD-DEV-SPEC", "Could not open device index %d.", dev_idx);
        ret = A_E_INVAL;
        goto done;
    }

    MFM_MSG(SEV_INFO, "DEV-IDX-OPEN", "Successfully opened device at index %d", dev_idx);

    TSL_BUG_ON(NULL == dev);

    /* Set the sample rate */
    MFM_MSG(SEV_INFO, "SAMPLE-RATE", "Setting sample rate to %u Hz", sample_rate);
    if (0 != rtlsdr_set_sample_rate(dev, sample_rate)) {
        MFM_MSG(SEV_ERROR, "BAD-SAMPLE-RATE", "Failed to set sample rate, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the center frequency */
    MFM_MSG(SEV_INFO, "CENTER-FREQ", "Setting Center Frequency to %u Hz", center_freq);
    if (0 != rtlsdr_set_center_freq(dev, center_freq)) {
        MFM_MSG(SEV_ERROR, "BAD-CENTER-FREQ", "Failed to set center frequency, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the gain, in deci-decibels */
    if (!FAILED(config_get_float(cfg, &gain_db, "dBGain"))) {
        /* Set the receiver gain based on the value in config */
        TSL_BUG_IF_FAILED(__rtl_sdr_worker_set_gain(dev, gain_db * 10));
    } else {
        MFM_MSG(SEV_INFO, "AUTO-GAIN-CONTROL", "Enabling automatic gain control.");
        TSL_BUG_ON(0 != rtlsdr_set_tuner_gain_mode(dev, 0));
    }

    /* Set the PPM correction, if specified */
    if (FAILED(config_get_integer(cfg, &ppm_corr, "ppmCorrection"))) {
        ppm_corr = 0;
    }

    if (0 != ppm_corr &&
            0 != (rtl_ret = rtlsdr_set_freq_correction(dev, ppm_corr)))
    {
        MFM_MSG(SEV_ERROR, "CANT-SET-FREQ-CORR", "Failed to set frequency "
                "correction to %d PPM (error: %d)", ppm_corr, rtl_ret);
        ret = A_E_INVAL;
        goto done;
    }

    MFM_MSG(SEV_INFO, "FREQ-CORR", "Set frequency correction to %d PPM", ppm_corr);

    /* Debug option: Dump the raw samples from the RTL SDR to a file on disk */
    if (!FAILED(config_get_string(cfg, &rtl_dump_file, "iqDumpFile"))) {
        /* Open the file, exclusive */
        if (0 > (dump_file_fd = open(rtl_dump_file, O_RDWR | O_CREAT | O_EXCL, 0666))) {
            int errnum = errno;
            MFM_MSG(SEV_INFO, "DUMP-FILE-FAIL", "Failed to create dump file '%s' - reason: '%s' (%d)",
                    rtl_dump_file, strerror(errnum), errnum);
            ret = A_E_INVAL;
            goto done;
        }
        MFM_MSG(SEV_INFO, "DUMP-TO-FILE", "Dumping raw I-Q samples as 8-bit interleaved to '%s'",
                rtl_dump_file);
    }

    /* Reset the endpoint */
    TSL_BUG_ON(0 != rtlsdr_reset_buffer(dev));

    DIAG("Gain set to: %f", (double)rtlsdr_get_tuner_gain(dev)/10.0);

    /* Create the worker thread context */
    if (FAILED(TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        ret = A_E_NOMEM;
        goto done;
    }

    thr->dev = dev;
    thr->muted = true;
    thr->samp_alloc = samp_buf_alloc;
    thr->dump_fd = dump_file_fd;
    list_init(&thr->demod_threads);

    /* Create the demodulator threads, walking the list of channels to be processed. */
    if (FAILED(ret = config_get(cfg, &channels, "channels"))) {
        MFM_MSG(SEV_ERROR, "MISSING-CHANNELS", "Need to specify at least one channel to demodulate.");
        ret = A_E_INVAL;
        goto done;
    }

    /* For debugging purposes, enable test mode. */
    if (!FAILED(ret = config_get_boolean(cfg, &test_mode, "sdrTestMode")) & (true == test_mode)) {
        MFM_MSG(SEV_INFO, "TEST-MODE", "Enabling RTL-SDR test mode");
        if (0 != rtlsdr_set_testmode(dev, 1)) {
            MFM_MSG(SEV_ERROR, "CANT-SET-TEST-MODE", "Failed to enable test mode, aborting.");
            ret = A_E_INVAL;
            goto done;
        }
    }

    CONFIG_ARRAY_FOR_EACH(channel, &channels, ret, arr_ctr) {
        const char *fifo_name = NULL;
        int nb_center_freq = -1;
        struct demod_thread *dmt = NULL;

        if (FAILED(ret = config_get_string(&channel, &fifo_name, "outFifo"))) {
            MFM_MSG(SEV_ERROR, "MISSING-FIFO-ID", "Missing output FIFO filename, aborting.");
            goto done;
        }

        if (FAILED(ret = config_get_integer(&channel, &nb_center_freq, "chanCenterFreq"))) {
            MFM_MSG(SEV_ERROR, "MISSING-CENTER-FREQ", "Missing output channel center frequency.");
            goto done;
        }

        DIAG("Center Frequency: %d Hz FIFO: %s", nb_center_freq, fifo_name);

        /* Create demodulator thread object */
        if (FAILED(demod_thread_new(&dmt, -1, samp_buf_alloc, (int32_t)nb_center_freq - center_freq,
                        sample_rate, fifo_name, decimation_factor, lpf_taps, lpf_nr_taps)))
        {
            MFM_MSG(SEV_ERROR, "FAILED-DEMOD-THREAD", "Failed to create demodulator thread, aborting.");
            goto done;
        }

        list_init(&dmt->dt_node);
        list_append(&thr->demod_threads, &dmt->dt_node);
        thr->nr_demod_threads++;
    }
    if (FAILED(ret)) {
        MFM_MSG(SEV_ERROR, "CHANNEL-SETUP-FAILURE", "Error reading array of channels, aborting.");
        goto done;
    }

    /* Initialize the worker thread */
    if (FAILED(ret = worker_thread_new(&thr->wthr, _rtl_sdr_worker_thread, WORKER_THREAD_CPU_MASK_ANY))) {
        MFM_MSG(SEV_ERROR, "THREAD-START-FAIL", "Failed to start worker thread, aborting.");
        goto done;
    }

    *pthr = thr;

done:
    if (NULL != lpf_taps) {
        TFREE(lpf_taps);
    }

    if (FAILED(ret)) {
        if (0 >= dump_file_fd) {
            close(dump_file_fd);
            dump_file_fd = -1;
        }

        if (NULL != dev) {
            rtlsdr_close(dev);
            dev = NULL;
        }
        if (NULL != thr) {
            TFREE(thr);
        }
    }
    return ret;
}

static
void _rtl_sdr_worker_thread_delete(struct rtl_sdr_thread **pthr)
{
    struct rtl_sdr_thread *thr = NULL;
    struct demod_thread *cur = NULL,
                        *tmp = NULL;

    if (NULL == pthr) {
        goto done;
    }

    thr = *pthr;

    if (NULL == thr) {
        goto done;
    }

    TSL_BUG_ON(0 != rtlsdr_cancel_async(thr->dev));
    TSL_BUG_IF_FAILED(worker_thread_request_shutdown(&thr->wthr));
    TSL_BUG_IF_FAILED(worker_thread_delete(&thr->wthr));

    list_for_each_type_safe(cur, tmp, &thr->demod_threads, dt_node) {
        list_del(&cur->dt_node);
        TSL_BUG_IF_FAILED(demod_thread_delete(&cur));
    }

    if (NULL != thr->dev) {
        DIAG("Releasing RTL-SDR device.");
        rtlsdr_close(thr->dev);
        thr->dev = NULL;
    }

    if (0 <= thr->dump_fd) {
        close(thr->dump_fd);
        thr->dump_fd = -1;
    }

    TFREE(thr);

done:
    return;
}

static
void _do_dump_rtl_sdr_devices(void)
{
    uint32_t nr_devs = rtlsdr_get_device_count();

    if (0 == nr_devs) {
        MFM_MSG(SEV_WARNING, "NO-DEVS-FOUND", "No RTL-SDR devices found.");
        goto done;
    }

    MFM_MSG(SEV_INFO, "DEVS-FOUND", "Found %u RTL-SDR devices", nr_devs + 1);

    for (uint32_t i = 0; i < nr_devs; i++) {
        const char *name = rtlsdr_get_device_name(i);
        fprintf(stderr, "%2u: %s\n", i, name);
    }

done:
    return;
}

static
void _usage(const char *name)
{
    fprintf(stderr, "usage: %s [Config File 1]{, Config File 2, ...} | %s -h\n", name, name);
    _do_dump_rtl_sdr_devices();
}

int main(int argc, const char *argv[])
{
    int ret = EXIT_FAILURE;
    struct config *cfg CAL_CLEANUP(config_delete) = NULL;
    struct rtl_sdr_thread *rtl_thr = NULL;
    uint32_t center_freq_hz = 0,
             sample_freq_hz = 0;
    int sr_hz = -1,
        center_hz = -1,
        nr_samp_bufs = -1;
    struct frame_alloc *falloc CAL_CLEANUP(frame_alloc_delete) = NULL;

    if (argc < 2) {
        _usage(argv[0]);
        goto done;
    }

    /* Parse and load the configurations from the command line */
    TSL_BUG_IF_FAILED(config_new(&cfg));

    for (int i = 1; i < argc; i++) {
        if (FAILED(config_add(cfg, argv[i]))) {
            MFM_MSG(SEV_FATAL, "MALFORMED-CONFIG", "Configuration file [%s] is malformed.", argv[i]);
            goto done;
        }
        DIAG("Added configuration file '%s'", argv[i]);
    }

    /* Initialize the app framework */
    TSL_BUG_IF_FAILED(app_init("multifm", cfg));
    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));

    /* Generate the demodulation states from configs (FIXME useful error messages) */
    sr_hz = 1000000ul; /* Always sample at 1MHz */
    TSL_BUG_IF_FAILED(config_get_integer(cfg, &center_hz, "centerFreqHz"));

    if (FAILED(config_get_integer(cfg, &nr_samp_bufs, "nrSampBufs"))) {
        MFM_MSG(SEV_INFO, "DEFAULT-SAMP-BUFS", "Setting sample buffer count to 64");
        nr_samp_bufs = 64;
    }

    center_freq_hz = center_hz;
    sample_freq_hz = sr_hz;

    MFM_MSG(SEV_INFO, "SAMPLE-RATE", "Sample rate is set to %u Hz", sample_freq_hz);
    MFM_MSG(SEV_INFO, "CENTER-FREQ", "Center Frequency is %u Hz", center_freq_hz);

    /*
     * Create the memory frame allocator for sample buffers
     */
    TSL_BUG_IF_FAILED(frame_alloc_new(&falloc,
                sizeof(struct sample_buf) +
                    RTL_SDR_DEFAULT_NR_SAMPLES * sizeof(int16_t) * 2,
                nr_samp_bufs));

    /* Prepare the RTL-SDR thread and demod threads */
    TSL_BUG_IF_FAILED(_rtl_sdr_worker_thread_new(cfg, center_freq_hz, sample_freq_hz, falloc, &rtl_thr));

    rtl_thr->muted = false;

    while (app_running()) {
        sleep(1);
    }

    DIAG("Terminating.");

    ret = EXIT_SUCCESS;
done:
    _rtl_sdr_worker_thread_delete(&rtl_thr);
    return ret;
}

