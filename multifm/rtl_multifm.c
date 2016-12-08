#include <multifm/multifm.h>
#include <multifm/sambuf.h>
#include <multifm/flex_fir_coeffs.h>
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

#include <rtl-sdr.h>

#define RTL_SDR_DEFAULT_NR_SAMPLES      (16 * 32 * 512/2)
#define LPF_PCM_OUTPUT_LEN              1024

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
    uint32_t nr_fm_samples;

    /**
     * FM samples to be processed
     */
    int32_t fm_samp_out_buf[2 * LPF_PCM_OUTPUT_LEN];

    /**
     * Number of good PCM samples
     */
    uint32_t nr_pcm_samples;

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

    TSL_BUG_IF_FAILED(direct_fir_push_sample_buf(&dthr->fir, sbuf));

    TSL_BUG_IF_FAILED(direct_fir_can_process(&dthr->fir, &can_process, NULL));

    while (true == can_process) {
        size_t nr_samples = 0;

        /* 1. Filter using FIR, decimate by the specified factor. Iterate over the output
         *    buffer samples.
         */
        TSL_BUG_IF_FAILED(direct_fir_process(&dthr->fir, dthr->fm_samp_out_buf + dthr->nr_fm_samples,
                    LPF_PCM_OUTPUT_LEN - dthr->nr_fm_samples, &nr_samples));

        dthr->nr_fm_samples += nr_samples;
        dthr->total_nr_fm_samples += nr_samples;

        /* 2. Perform quadrature demod, write to output demodulation buffer. */
        /* TODO: smarten this up a lot - this sucks */
        dthr->nr_pcm_samples = 0;

        for (size_t i = 1; i < dthr->nr_fm_samples; i++) {
            int32_t a_re = dthr->fm_samp_out_buf[2 * (i - 1)    ],
                    a_im = dthr->fm_samp_out_buf[2 * (i - 1) + 1],
                    b_re = dthr->fm_samp_out_buf[2 *  i         ],
                    b_im = dthr->fm_samp_out_buf[2 *  i      + 1];
            int32_t s_re = a_re * b_re - a_im * b_im,
                    s_im = a_im * b_re + a_re * b_im;
            double sample = atan2((double)s_im, (double)s_re);

            dthr->pcm_out_buf[dthr->nr_pcm_samples] = (int64_t)(0.5*(sample/M_PI) * (1ll << 14));
#if 0
            dthr->pcm_out_buf[dthr->nr_pcm_samples * 2] = (int64_t)(dthr->fm_samp_out_buf[2 * i]);
            dthr->pcm_out_buf[dthr->nr_pcm_samples * 2 + 1] = (int64_t)(dthr->fm_samp_out_buf[2 * i + 1]);
#endif

            dthr->nr_pcm_samples++;
        }

        // XXX HACK
        //write(dthr->fifo_fd, dthr->pcm_out_buf, dthr->nr_pcm_samples * sizeof(uint16_t));
        write(dthr->fifo_fd, dthr->fm_samp_out_buf + 1, (dthr->nr_fm_samples - 1) * sizeof(uint32_t) * 2);

        /* XXX move the last sample of the FM buffer to be the first sample; we'll use it
         * for the next round of quadrature demodulation.
         */
        dthr->nr_fm_samples = 1;
        dthr->fm_samp_out_buf[0] = dthr->fm_samp_out_buf[dthr->nr_fm_samples - 1];

        /* 3. Stretch goal: resample 25kHz to 22,050kHz */

        /* 4. Super Stretch Goal: integrate FLEX demod into this */

        TSL_BUG_IF_FAILED(direct_fir_can_process(&dthr->fir, &can_process, NULL));
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
 * \param offset_hz The offset, in hertz, from the center frequency
 * \param sample_rate The sample rate of the input stream
 * \param decimation The decimation factor for the output from this FIR.
 *
 * \return A_OK on success, an error code otherwise
 */
static
aresult_t _demod_fir_prepare(struct demod_thread *thr, int32_t offset_hz, uint32_t sample_rate, int decimation)
{
    aresult_t ret = A_OK;

    int32_t *coeffs = NULL;
    int64_t power = 0;
    double f_offs = 2.0 * M_PI * (double)offset_hz / (double)sample_rate,
           dpower = 0.0;
    size_t base = LPF_NR_COEFFS;

    DIAG("Preparing LPF for offset %d Hz", offset_hz);

    TSL_ASSERT_ARG(NULL != thr);

    if (FAILED(ret = TACALLOC((void *)&coeffs, LPF_NR_COEFFS, sizeof(int32_t) * 2, SYS_CACHE_LINE_LENGTH))) {
        MFM_MSG(SEV_FATAL, "NO-MEM", "Out of memory for FIR.");
        goto done;
    }

    fprintf(stderr, "lpf_shifted_%d = [\n", offset_hz);
    for (size_t i = 0; i < LPF_NR_COEFFS; i++) {
        /* Calculate the new tap coefficient */
        double complex lpf_tap = cexp(CMPLX(0, -f_offs * (double)i)) * lpf_taps[i];
        double q31 = 1ll << 31,
               ptemp = 0;
        int64_t samp_power = 0;

        /* Calculate the Q31 coefficient */
        coeffs[       i] = (int32_t)(creal(lpf_tap) * q31 + 0.5);
        coeffs[base + i] = (int32_t)(cimag(lpf_tap) * q31 + 0.5);

        ptemp = sqrt( (creal(lpf_tap) * creal(lpf_tap)) + (cimag(lpf_tap) * cimag(lpf_tap)) );

        samp_power = sqrt( ((int64_t)coeffs[i] * (int64_t)coeffs[i]) + ((int64_t)coeffs[base + i] * (int64_t)coeffs[base + i]) ) + 0.5;

        power += samp_power;
        dpower += ptemp;

        fprintf(stderr, "    complex(%d, %d), %% (%f, %f) P: 0x%016zx ~~ %f\n", coeffs[i], coeffs[base + i], creal(lpf_tap), cimag(lpf_tap), samp_power, ptemp);
    }
    fprintf(stderr, "];\n");
    fprintf(stderr, "%% Total power: %zu (%016zx) (%f)\n", power, power, dpower);

    /* Create a Direct Type FIR implementation */
    TSL_BUG_IF_FAILED(direct_fir_init(&thr->fir, LPF_NR_COEFFS, coeffs, &coeffs[base], decimation, thr));

done:
    if (NULL != coeffs) {
        TFREE(coeffs);
    }

    return ret;
}

static
aresult_t demod_thread_new(struct demod_thread **pthr, unsigned core_id, struct frame_alloc *samp_buf_alloc,
        int32_t offset_hz, uint32_t samp_hz, const char *out_fifo, int decimation_factor)
{
    aresult_t ret = A_OK;

    struct demod_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != samp_buf_alloc);
    TSL_ASSERT_ARG(NULL != out_fifo && '\0' != *out_fifo);

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
    if (FAILED(ret = _demod_fir_prepare(thr, offset_hz, samp_hz, decimation_factor))) {
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

    int16_t min = INT16_MAX,
            max = INT16_MIN;
    int64_t total = 0,
            total_squared = 0;

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

    /* Up-convert the u8 samples to s16, subtract 127 to get actual power */
    for (size_t i = 0; i < len; i++) {
        sbuf_ptr[i] = (int16_t)buf[i] - 127;
    }

    for (size_t i = 0; i < len/2; i++) {
        double samp = sqrt(sbuf_ptr[2 * i] * sbuf_ptr[2 * i] +
                sbuf_ptr[2 * i + 1] * sbuf_ptr[2 * i + 1]);
        min = BL_MIN2(min, samp);
        max = BL_MAX2(max, samp);
        total += samp + 0.5;
        total_squared += samp * samp + 0.5;
    }

    printf("min: %d max: %d mean: %f\n", (int)min, (int)max, (double)total/(double)len);

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
        if (real_gain > gain) {
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
        gain_ddb = 0,
        ppm_corr = 0,
        rtl_ret = 0,
        decimation_factor = 0,
        dump_file_fd = -1;
    size_t arr_ctr = 0;
    bool test_mode = false;


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
    if (!FAILED(config_get_integer(cfg, &gain_ddb, "gaindDb"))) {
        /* Set the receiver gain based on the value in config */
        TSL_BUG_IF_FAILED(__rtl_sdr_worker_set_gain(dev, gain_ddb));
    } else {
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
                        sample_rate, fifo_name, decimation_factor)))
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

