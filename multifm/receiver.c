#include <multifm/receiver.h>
#include <multifm/demod.h>
#include <multifm/multifm.h>

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/list.h>
#include <tsl/worker_thread.h>
#include <tsl/frame_alloc.h>

#include <stdatomic.h>

/**
 * Free a live sample buffer.
 *
 * \param buf The buffer to free
 * 
 * \return A_OK on success, an error code otherwise.
 */
static
aresult_t _sample_buf_release(struct sample_buf *buf)
{
    aresult_t ret = A_OK;

    struct frame_alloc *fa = NULL;

    TSL_ASSERT_ARG(NULL != buf);
    TSL_BUG_ON(atomic_load(&buf->refcount) != 0);

    fa = buf->priv;

    TSL_BUG_IF_FAILED(frame_free(fa, (void **)&buf));

    return ret;
}

/**
 * Allocate a sample buffer
 */
aresult_t receiver_sample_buf_alloc(struct receiver *rx, struct sample_buf **pbuf)
{
    aresult_t ret = A_OK;

    struct sample_buf *sbuf = NULL;

    TSL_ASSERT_ARG(NULL != rx);
    TSL_ASSERT_ARG(NULL != pbuf);

    *pbuf = NULL;

    /* Allocate an output buffer */
    if (FAILED(ret = frame_alloc(rx->samp_alloc, (void **)&sbuf))) {
        if (0 == rx->nr_samp_buf_alloc_fails) {
            MFM_MSG(SEV_INFO, "NO-SAMPLE-BUFFER", "There are no available sample buffers, dropping received samples.");
        }
        rx->nr_samp_buf_alloc_fails++;
        goto done;
    }

    /* Initialize the state for the sample buffer */
    sbuf->release = _sample_buf_release;
    sbuf->priv = rx->samp_alloc;

    *pbuf = sbuf;

done:
    return ret;
}

/**
 * Deliver a sample buffer to any waiting consumers
 */
aresult_t receiver_sample_buf_deliver(struct receiver *rx, struct sample_buf *buf)
{
    aresult_t ret = A_OK;

    struct demod_thread *dthr = NULL;

    atomic_store(&buf->refcount, rx->nr_demod_threads);

    /* Make it available to each demodulator/processing thread */
    list_for_each_type(dthr, &rx->demod_threads, dt_node) {
        pthread_mutex_lock(&dthr->wq_mtx);
        TSL_BUG_IF_FAILED(work_queue_push(&dthr->wq, buf));
        pthread_mutex_unlock(&dthr->wq_mtx);
        /* Signal there is data ready, if the thread is waiting on the condvar */
        pthread_cond_signal(&dthr->wq_cv);
    }

    return ret;
}

aresult_t receiver_init(struct receiver *rx, struct config *cfg,
        receiver_rx_thread_func_t rx_func, receiver_cleanup_func_t cleanup_func,
        size_t samples_per_buf)
{
    aresult_t ret = A_OK;

    double *lpf_taps = NULL,
           *resample_filter_taps CAL_CLEANUP(free_double_array) = NULL;
    double dc_block_pole = 0.9999;

    size_t lpf_nr_taps = 0,
           arr_ctr = 0,
           nr_resample_filter_taps = 0;
    int decimation_factor = 0,
        resample_decimate = 0,
        resample_interpolate = 0,
        nr_samp_bufs = 0,
        sample_rate = 0,
        center_freq = 0;
    bool enable_dc_block = false;
    int16_t *resample_int_filter_taps CAL_CLEANUP(free_i16_array) = NULL;

    struct config rational_resampler,
                  channels,
                  channel;

    struct frame_alloc *sample_buf_alloc = NULL;

    TSL_ASSERT_ARG(NULL != rx);
    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != rx_func);
    TSL_ASSERT_ARG(NULL != cleanup_func);
    TSL_ASSERT_ARG(0 != samples_per_buf);

    rx->muted = true;
    rx->samp_alloc = sample_buf_alloc;
    rx->cleanup_func = cleanup_func;
    rx->thread_func = rx_func;

    if (FAILED(ret = config_get_integer(cfg, &nr_samp_bufs, "nrSampBufs"))) {
        MFM_MSG(SEV_INFO, "DEFAULT-SAMP-BUFS", "Setting sample buffer count to 64");
        nr_samp_bufs = 64;
    }

    if (FAILED(ret = config_get_integer(cfg, &sample_rate, "sampleRateHz"))) {
        MFM_MSG(SEV_INFO, "NO-SAMPLE-RATE", "Need to specify a sample rate, in Hertz.");
        goto done;
    }

    if (FAILED(ret = config_get_integer(cfg, &center_freq, "centerFreqHz"))) {
        MFM_MSG(SEV_INFO, "NO-CENTER-FREQ", "You forgot to specify a center frequency, in Hz.");
        goto done;
    }

    MFM_MSG(SEV_INFO, "SAMPLE-RATE", "Sample rate is set to %u Hz", sample_rate);
    MFM_MSG(SEV_INFO, "CENTER-FREQ", "Center Frequency is %u Hz", center_freq);

    /*
     * Create the memory frame allocator for sample buffers
     */
    TSL_BUG_IF_FAILED(frame_alloc_new(&rx->samp_alloc,
                sizeof(struct sample_buf) +
                    samples_per_buf * sizeof(int16_t) * 2,
                nr_samp_bufs));

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

    /* Grab the rational resampler taps, if appropriate */
    if (!FAILED(config_get(cfg, &rational_resampler, "rationalResampler"))) {
        DIAG("Preparing the rational resampler.");

        if (FAILED(ret = config_get_integer(&rational_resampler, &resample_decimate, "decimate"))) {
            MFM_MSG(SEV_ERROR, "MISSING-DECIMATE", "Need to specify the decimation factor for the rational resampler.");
            goto done;
        }

        if (0 >= resample_decimate) {
            ret = A_E_INVAL;
            MFM_MSG(SEV_ERROR, "BAD-DECIMATION-FACTOR", "The decimation factor for the rational resampler must be a non-zero positive integer.");
            goto done;
        }

        if (FAILED(ret = config_get_integer(&rational_resampler, &resample_interpolate, "interpolate"))) {
            MFM_MSG(SEV_ERROR, "MISSING-INTERPOLATE", "Need to specify the interpolation factor for the rational resampler.");
            goto done;
        }

        if (0 >= resample_interpolate) {
            ret = A_E_INVAL;
            MFM_MSG(SEV_ERROR, "BAD-INTERPOLATION-FACTOR", "The interpolation factor for the rational resampler must be a non-zero positive integer.");
            goto done;
        }

        if (FAILED(ret = config_get_float_array(&rational_resampler, &resample_filter_taps, &nr_resample_filter_taps, "lpfCoeffs"))) {
            MFM_MSG(SEV_ERROR, "MISSING-RESAMPLE-FILTER-COEFF", "Missing filter coefficients for the resampling filter.");
            goto done;
        }

        if (0 == nr_resample_filter_taps || resample_interpolate > nr_resample_filter_taps) {
            ret = A_E_INVAL;
            MFM_MSG(SEV_ERROR, "NO-RESAMPLE-TAPS", "Rational resampler filter taps must not be empty or there must be enough taps to perform an interpolation.");
            goto done;
        }

        TSL_BUG_IF_FAILED(TCALLOC((void **)&resample_int_filter_taps, sizeof(int16_t), nr_resample_filter_taps));

        for (size_t i = 0; i < nr_resample_filter_taps; i++) {
            double q15 = 1 << Q_15_SHIFT;
            resample_int_filter_taps[i] = resample_filter_taps[i] * q15;
        }

        TFREE(resample_filter_taps);

        MFM_MSG(SEV_INFO, "RATIONAL-RESAMPLER", "Using Rational Resampler with to resample the output rate to %d/%d with %zu taps in filter.",
                resample_interpolate, resample_decimate, nr_resample_filter_taps);
    }

    if (!FAILED(ret = config_get_boolean(cfg, &enable_dc_block, "enableDCBlocker"))) {
        if (true == enable_dc_block) {
            if (FAILED(ret = config_get_float(cfg, &dc_block_pole, "dcBlockerPole"))) {
                dc_block_pole = 0.9999;
            }

            MFM_MSG(SEV_INFO, "DC-BLOCK-ENABLE", "Enabled DC Blocker. Using pole value %f.", dc_block_pole);
        }
    }

    list_init(&rx->demod_threads);

    /* Create the demodulator threads, walking the list of channels to be processed. */
    if (FAILED(ret = config_get(cfg, &channels, "channels"))) {
        MFM_MSG(SEV_ERROR, "MISSING-CHANNELS", "Need to specify at least one channel to demodulate.");
        ret = A_E_INVAL;
        goto done;
    }

    CONFIG_ARRAY_FOR_EACH(channel, &channels, ret, arr_ctr) {
        const char *fifo_name = NULL,
                   *signal_debug = NULL;
        int nb_center_freq = -1;
        struct demod_thread *dmt = NULL;
        double channel_gain = 1.0,
               channel_gain_db = 0.0;

        if (FAILED(ret = config_get_string(&channel, &fifo_name, "outFifo"))) {
            MFM_MSG(SEV_ERROR, "MISSING-FIFO-ID", "Missing output FIFO filename, aborting.");
            goto done;
        }

        if (FAILED(ret = config_get_integer(&channel, &nb_center_freq, "chanCenterFreq"))) {
            MFM_MSG(SEV_ERROR, "MISSING-CENTER-FREQ", "Missing output channel center frequency.");
            goto done;
        }

        if (!FAILED(ret = config_get_string(&channel, &signal_debug, "signalDebugFile"))) {
            MFM_MSG(SEV_INFO, "WRITING-SIGNAL-DEBUG", "The channel at frequency %d will have raw I/Q written to '%s'",
                    nb_center_freq, signal_debug);
        }

        if (!FAILED(ret = config_get_float(&channel, &channel_gain_db, "dBGain"))) {
            /* Convert the gain to linear units */
            channel_gain = pow(10.0, channel_gain_db/10.0);
            DIAG("Setting input channel gain to: %f (%f dB)", channel_gain, channel_gain_db);
        }

        DIAG("Center Frequency: %d Hz FIFO: %s", nb_center_freq, fifo_name);

        /* Create demodulator thread object */
        if (FAILED(ret = demod_thread_new(&dmt, -1, (int32_t)nb_center_freq - center_freq,
                        sample_rate, fifo_name, decimation_factor, lpf_taps, lpf_nr_taps,
                        resample_decimate, resample_interpolate, resample_int_filter_taps,
                        nr_resample_filter_taps,
                        signal_debug,
                        dc_block_pole, enable_dc_block,
                        channel_gain)))
        {
            MFM_MSG(SEV_ERROR, "FAILED-DEMOD-THREAD", "Failed to create demodulator thread, aborting.");
            goto done;
        }

        list_init(&dmt->dt_node);
        list_append(&rx->demod_threads, &dmt->dt_node);
        rx->nr_demod_threads++;

        MFM_MSG(SEV_INFO, "CHANNEL", "[%zu]: %4.5f MHz Gain: %f dB -> [%s]%s%s",
                rx->nr_demod_threads, (double)nb_center_freq/1e6, channel_gain_db, fifo_name,
                (NULL != signal_debug ? " DEBUG: " : ""),
                (NULL != signal_debug ? signal_debug : ""));
    }
    if (FAILED(ret)) {
        MFM_MSG(SEV_ERROR, "CHANNEL-SETUP-FAILURE", "Error reading array of channels, aborting.");
        goto done;
    }

done:
    if (NULL != lpf_taps) {
        TFREE(lpf_taps);
    }

    return ret;
}

static
aresult_t _receiver_worker_thread(struct worker_thread *wthr)
{
    struct receiver *rx = BL_CONTAINER_OF(wthr, struct receiver, wthr);

    TSL_BUG_ON(NULL == rx->thread_func);

    return rx->thread_func(rx);
}

aresult_t receiver_start(struct receiver *rx)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != rx);

    if (FAILED(ret = worker_thread_new(&rx->wthr, _receiver_worker_thread, WORKER_THREAD_CPU_MASK_ANY))) {
        MFM_MSG(SEV_ERROR, "THREAD-START-FAIL", "Failed to start worker thread, aborting.");
        goto done;
    }

done:
    return ret;
}

aresult_t receiver_cleanup(struct receiver **prx)
{
    aresult_t ret = A_OK;

    struct receiver *rx = NULL;
    struct demod_thread *cur = NULL,
                        *tmp = NULL;

    TSL_ASSERT_ARG(NULL != prx);
    TSL_ASSERT_ARG(NULL != *prx);

    rx = *prx;

    /* Clean up the receiver state */
    TSL_BUG_IF_FAILED(rx->cleanup_func(rx));

    /* Shut down the worker thread */
    TSL_BUG_IF_FAILED(worker_thread_request_shutdown(&rx->wthr));
    TSL_BUG_IF_FAILED(worker_thread_delete(&rx->wthr));

    list_for_each_type_safe(cur, tmp, &rx->demod_threads, dt_node) {
        list_del(&cur->dt_node);
        TSL_BUG_IF_FAILED(demod_thread_delete(&cur));
    }

    TSL_BUG_IF_FAILED(frame_alloc_delete(&rx->samp_alloc));

    return ret;
}

aresult_t receiver_set_mute(struct receiver *rx, bool mute)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != rx);

    rx->muted = mute;

    return ret;
}

bool receiver_thread_running(struct receiver *rx)
{
    TSL_BUG_ON(NULL == rx);

    return worker_thread_is_running(&rx->wthr);
}

