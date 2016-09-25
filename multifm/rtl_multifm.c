#include <multifm/multifm.h>
#include <multifm/sambuf.h>
#include <multifm/sring.h>

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
#include <pthread.h>

#include <rtl-sdr.h>

#define RTL_SDR_DEFAULT_NR_SAMPLES      (16 * 32 * 512)

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
};

static
aresult_t demod_thread_samp_buf_decref(struct demod_thread *thr, struct sample_buf *buf)
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
aresult_t demod_thread_process(struct demod_thread *dthr, const struct sample_buf *sbuf)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != dthr);
    TSL_ASSERT_ARG(NULL != sbuf);

    /* 1. DDC multiply by e(-2\pi*f) */

    /* 2. CIC filter to act as a LPF/downsample */

    /* 3. Perform FM demod, write to output PCM buffer */

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
            TSL_BUG_IF_FAILED(demod_thread_samp_buf_decref(dthr, buf));

            /* Re-acquire the lock */
            pthread_mutex_lock(&dthr->wq_mtx);
        } else {
            /* Wait until the acquisition thread wakes us up */
            int pt_en = 0;
            struct timespec tm = { .tv_sec = 1, .tv_nsec = 0 };
            if (0 != (pt_en = pthread_cond_timedwait(&dthr->wq_cv, &dthr->wq_mtx, &tm))) {
                MFM_MSG(SEV_FATAL, "DEMOD-THREAD-FAIL", "Demodulator thread "
                        "failed to acquire condvar. Reason: %s (%d)",
                        strerror(pt_en), pt_en);
                /* XXX: Panic? */
                goto done;
            }
        }
    }

done:
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

    TFREE(thr);

    *pthr = NULL;

    return ret;
}

static
aresult_t demod_thread_new(struct demod_thread **pthr, unsigned core_id, struct frame_alloc *samp_buf_alloc)
{
    aresult_t ret = A_OK;

    struct demod_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != samp_buf_alloc);

    *pthr = NULL;

    if (FAILED(ret = TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

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

    thr->samp_buf_alloc = samp_buf_alloc;

    list_init(&thr->dt_node);

    TSL_BUG_IF_FAILED(worker_thread_new(&thr->wthr, _demod_thread_work, core_id));

done:
    if (FAILED(ret)) {
        if (NULL != thr) {
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

    DIAG("Got %u samples", len);

    if (true == thr->muted) {
        /* If the receiver side is muted, there's no need to process this buffer */
        goto done;
    }

    /* Allocate an output buffer */
    if (FAILED(frame_alloc(thr->samp_alloc, (void **)sbuf))) {
        MFM_MSG(SEV_INFO, "NO-SAMPLE-BUFFER", "Out of sample buffers.");
        goto done;
    }

    sbuf_ptr = (int16_t *)sbuf->data_buf;

    /* Up-convert the u8 samples to s16, subtract 127 to get actual power */
    for (size_t i = 0; i < len; i++) {
        sbuf_ptr[i * 2] = (int16_t)buf[i * 2] - 127;
        sbuf_ptr[i * 2 + 1] = (int16_t)buf[i * 2 + 1] - 127;
    }

    /* Apply the DC filtering */

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

    struct rtl_sdr_thread *thr = NULL;
    struct rtlsdr_dev *dev = NULL;
    int dev_idx = -1,
        rret = 0,
        gain_ddb = 0,
        ppm_corr = 0,
        rtl_ret = 0;


    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != samp_buf_alloc);

    *pthr = NULL;

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

#if 0
    /* Disable AGC */
    if (0 != rtlsdr_set_agc_mode(dev, 0)) {
        MFM_MSG(SEV_WARNING, "CANT-SET-AGC", "Failed to disable AGC.");
    }
#endif

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

    /* Reset the endpoint */
    TSL_BUG_ON(0 != rtlsdr_reset_buffer(dev));

    /* Set the center frequency */
    MFM_MSG(SEV_INFO, "CENTER-FREQ", "Setting Center Frequency to %u Hz", center_freq);
    if (0 != rtlsdr_set_center_freq(dev, center_freq)) {
        MFM_MSG(SEV_ERROR, "BAD-CENTER-FREQ", "Failed to set center frequency, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the sample rate */
    MFM_MSG(SEV_INFO, "SAMPLE-RATE", "Setting sample rate to %u Hz", sample_rate);
    if (0 != rtlsdr_set_sample_rate(dev, sample_rate)) {
        MFM_MSG(SEV_ERROR, "BAD-SAMPLE-RATE", "Failed to set sample rate, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    /* XXX: Create the demodulator threads, walking the list of channels to be
     * processed.
     */

    /* Create the worker thread context */
    if (FAILED(TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        ret = A_E_NOMEM;
        goto done;
    }

    thr->dev = dev;
    thr->muted = true;
    thr->samp_alloc = samp_buf_alloc;

    /* Initialize the worker thread */
    if (FAILED(ret = worker_thread_new(&thr->wthr, _rtl_sdr_worker_thread, WORKER_THREAD_CPU_MASK_ANY))) {
        MFM_MSG(SEV_ERROR, "THREAD-START-FAIL", "Failed to start worker thread, aborting.");
        goto done;
    }

    *pthr = thr;

done:
    if (FAILED(ret)) {
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

    if (NULL != thr->dev) {
        DIAG("Releasing RTL-SDR device.");
        rtlsdr_close(thr->dev);
        thr->dev = NULL;
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
    struct rtl_sdr_thread *rtl_thr CAL_CLEANUP(_rtl_sdr_worker_thread_delete) = NULL;
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
    TSL_BUG_IF_FAILED(config_get_integer(cfg, &sr_hz, "sampleRateHz"));
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

    /* Prepare the RTL-SDR thread */
    TSL_BUG_IF_FAILED(_rtl_sdr_worker_thread_new(cfg, center_freq_hz, sample_freq_hz, falloc, &rtl_thr));

    while (app_running()) {
        sleep(1);
    }

    DIAG("Terminating.");

    ret = EXIT_SUCCESS;
done:
    return ret;
}

