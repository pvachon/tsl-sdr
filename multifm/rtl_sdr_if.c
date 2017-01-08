/*
 *  rtl_sdr_if.c - RTL-SDR Interface for samples, in multifm.
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <multifm/rtl_sdr_if.h>
#include <multifm/multifm.h>
#include <multifm/demod.h>

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <tsl/list.h>
#include <tsl/worker_thread.h>
#include <tsl/assert.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/frame_alloc.h>

#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _USE_ARM_NEON
#include <arm_neon.h>
#endif

#include <rtl-sdr.h>

#define RTL_SDR_CONVERSION_SHIFT        5

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

    sbuf->release = _sample_buf_release;
    sbuf->priv = thr->samp_alloc;

    sbuf_ptr = (int16_t *)sbuf->data_buf;

    /* Up-convert the u8 samples to Q.15, subtract 127 from the unsigned sample to get actual power */
#ifdef _USE_ARM_NEON
    int16x8_t samples,
              sub_const  = { 127, 127, 127, 127, 127, 127, 127, 127 };
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
        samples = vqshlq_n_s16(samples, RTL_SDR_CONVERSION_SHIFT);

        /* Store in the output buffer at the appropriate location */
        vst1q_s16(sbuf_ptr + offs, samples);
    }

    /* If there's a remainder because the sample count is not divisible by 8, process the remainder */
    size_t buf_offs = len & ~(8 - 1);

    for (size_t i = 0; i < len % 8; i++) {
        sbuf_ptr[i + buf_offs] = ((int16_t)buf[i + buf_offs] - 127) << RTL_SDR_CONVERSION_SHIFT;
    }

#else /* Works for any architecture */
    for (size_t i = 0; i < len; i++) {
        sbuf_ptr[i] = ((int16_t)buf[i] - 127) << RTL_SDR_CONVERSION_SHIFT;
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
aresult_t rtl_sdr_worker_thread_new(
        struct config *cfg,
        uint32_t center_freq,
        uint32_t sample_rate,
        struct frame_alloc *samp_buf_alloc,
        struct rtl_sdr_thread **pthr)
{
    aresult_t ret = A_OK;

    struct config channel = CONFIG_INIT_EMPTY,
                  channels = CONFIG_INIT_EMPTY,
                  rational_resampler = CONFIG_INIT_EMPTY;
    struct rtl_sdr_thread *thr = NULL;
    struct rtlsdr_dev *dev = NULL;
    const char *rtl_dump_file = NULL;
    int dev_idx = -1,
        rret = 0,
        ppm_corr = 0,
        rtl_ret = 0,
        decimation_factor = 0,
        dump_file_fd = -1,
        resample_decimate = 0,
        resample_interpolate = 0;
    size_t arr_ctr = 0,
           lpf_nr_taps = 0,
           nr_resample_filter_taps = 0;
    bool test_mode = false;
    double *lpf_taps = NULL,
           *resample_filter_taps = NULL;
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

        if (FAILED(ret = config_get_float_array(&rational_resampler, &resample_filter_taps, &nr_resample_filter_taps, "filterCoefficients"))) {
            MFM_MSG(SEV_ERROR, "MISSING-RESAMPLE-FILTER-COEFF", "Missing filter coefficients for the resampling filter.");
            goto done;
        }

        if (0 == nr_resample_filter_taps || resample_interpolate > nr_resample_filter_taps) {
            ret = A_E_INVAL;
            MFM_MSG(SEV_ERROR, "NO-RESAMPLE-TAPS", "Rational resampler filter taps must not be empty or there must be enough taps to perform an interpolation.");
            goto done;
        }

        MFM_MSG(SEV_INFO, "RATIONAL-RESAMPLER", "Using Rational Resampler with to resample the output rate to %d/%d with %zu taps in filter.",
                resample_interpolate, resample_decimate, nr_resample_filter_taps);
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
        if (FAILED(demod_thread_new(&dmt, -1, (int32_t)nb_center_freq - center_freq,
                        sample_rate, fifo_name, decimation_factor, lpf_taps, lpf_nr_taps,
                        resample_decimate, resample_interpolate, resample_filter_taps,
                        nr_resample_filter_taps)))
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

void rtl_sdr_worker_thread_delete(struct rtl_sdr_thread **pthr)
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

