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

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <tsl/assert.h>
#include <tsl/errors.h>
#include <tsl/diag.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#ifdef _USE_ARM_NEON
#include <arm_neon.h>
#endif

#include <rtl-sdr.h>

#define RTL_SDR_CONVERSION_SHIFT        7
#define RTL_SDR_DEFAULT_NR_SAMPLES      (16 * 32 * 512/2)

static
aresult_t _rtl_sdr_worker_thread_delete(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct rtl_sdr_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != rx);

    thr = BL_CONTAINER_OF(rx, struct rtl_sdr_thread, rx);

    TSL_BUG_ON(0 != rtlsdr_cancel_async(thr->dev));

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

    if (true == thr->rx.muted) {
        DIAG("Worker is muted.");
        /* If the receiver side is muted, there's no need to process this buffer */
        goto done;
    }

    if (0 <= thr->dump_fd) {
        if (0 > write(thr->dump_fd, buf, len)) {
            DIAG("Failed to write %u bytes to the RTL-SDR dump file.", len);
        }
    }

    if (FAILED(receiver_sample_buf_alloc(&thr->rx, &sbuf))) {
        goto done;
    }

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

    TSL_BUG_IF_FAILED(receiver_sample_buf_deliver(&thr->rx, sbuf));

done:
    return;
}

static
aresult_t _rtl_sdr_worker_thread(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct rtl_sdr_thread *thr = BL_CONTAINER_OF(rx, struct rtl_sdr_thread, rx);
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
aresult_t __rtl_sdr_worker_e4000_set_if_gain(struct rtlsdr_dev *dev, int if_gain_tenths)
{
    aresult_t ret = A_OK;

    /* Initial gains for each stage, in 10th's of a dB */
    int gains[6] = { -30, 0, 0, 0, 30, 30 };
    /* Steps */
    int steps[6] = { 90, 30, 30, 10, 30, 30 };
    int max[6] = { 60, 90, 90, 20, 150, 150 };

    /* Current gain */
    int cur_gain = 30,
        last_gain = 0;

    TSL_ASSERT_ARG(NULL != dev);

    /* Continue to iterate until no changes occur */
    while (last_gain != cur_gain) {
        last_gain = cur_gain;
        /* Walk each IF stage and try to adjust the gain accordingly */
        for (int i = 0; i < 6; i++) {
            if (steps[i] + gains[i] > max[i]) {
                continue;
            }

            if (if_gain_tenths - cur_gain > steps[i]) {
                gains[i] += steps[i];
                cur_gain += steps[i];
            }
        }
    }

    DIAG("Desired gain: %d Selected gain: %d", if_gain_tenths, cur_gain);
    DIAG("Gains: { %d, %d, %d, %d, %d, %d }", gains[0], gains[1], gains[2], gains[3], gains[4], gains[5]);

    for (int i = 0; i < 6; i++) {
        if (0 != rtlsdr_set_tuner_if_gain(dev, i + 1, gains[i])) {
            MFM_MSG(SEV_WARNING, "FAILED-SETTING-GAIN", "Failed to set IF gain stage %d to value %d",
                    i + 1, gains[i]);
        }
    }

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

/* Helpful constants for outputing human-readable log messages */
static const
char *__rtl_sdr_tuner_names[] = {
    [RTLSDR_TUNER_UNKNOWN] = "Unknown Tuner Type",
    [RTLSDR_TUNER_E4000] = "Elonics E4000",
    [RTLSDR_TUNER_FC0012] = "Fitipower FC0012",
    [RTLSDR_TUNER_FC0013] = "Fitipower FC0013",
    [RTLSDR_TUNER_FC2580] = "Fitipower FC2580",
    [RTLSDR_TUNER_R820T] = "Rafael Micro R820T",
    [RTLSDR_TUNER_R828D] = "Rafael Micro R828D",
};


/** 
 * An RTL-SDR device can be identified by either the serial number read from
 * the RTL-SDR eeprom or by the less predictable device index. The search
 * procedure is now:
 * 
 *  1. If deviceSerial is present, use the lowest RTL-SDR index with matching serial or fail.
 *      To remain largely consistent with librtlsdr's verbose_device_search serial matching, the
 *      serial matching is as follows:
 *      1a. Accept first device serial that matches user provided serial.
 *      1b. Accept first device serial that matches user provided serial as a prefix.
 *      1c. Accept first device serial that matches user provided serial as a suffix.
 *      1d. To maintain consistent behavior with deviceIndex search, fail.
 *  2. If deviceIndex is present, attempt to use the indicated RTL-SDR or fail.
 *      2a. To maintain compatible behavior, if deviceIndex is present and can't be opened, fail.
 *  3. As a last resort, use the first RTL-SDR that can be opened or fail.
 *
 * * Notes on RTLSDR EEPROM and serial numbers:
 *
 * There is no guarantee that device serial numbers are unique.
 *
 * The maximum length of a serial number is not defined. The limit in the EEPROM is
 * 32 UTF-16 bytes shared between three string descriptors, manufacturer, product and
 * serial. The code in librtlsdr has no support for unicode and uses the ASCII variants
 * of the libusb string descriptor functions.
 *
 * The purpose of the prefix and suffix serial matching is for people who have sets of
 * devices prepared for specific uses where any device in that set is equivalent within
 * that set. The device serial numbers are a concatenation of a common set name and a
 * unique device identifier. E.g. `VHFAIR0`, `VHFAIR1`, `UHFAIR0`, `UHFAIR1`. By
 * configuring software to use a device with serial `VHF` either `VHFAIR0` or `VHFAIR1`
 * could be selected if available.
 */

static
aresult_t __rtl_sdr_device_search(const int user_idx, const char *user_serial, int *out_idx, rtlsdr_dev_t **out_rtldev)
{
    aresult_t ret = A_OK;

    int device_count = -1;
    int device_iter = 0;
    char device_serial[256];

    TSL_ASSERT_ARG(NULL != out_rtldev);

    device_count = rtlsdr_get_device_count();

    if (device_count < 1) {
        ret = A_E_NOTFOUND;
        goto done;
    }

    if (NULL != user_serial) {

        /* Search for an exact match */
        for (device_iter = 0; device_iter < device_count; device_iter++) {
            if (rtlsdr_get_device_usb_strings(device_iter, NULL, NULL, device_serial) < 0) continue;

            if (!strncmp(device_serial, user_serial, 256) && !rtlsdr_open(out_rtldev, device_iter))
                goto found;
        }

        /* Search for user_serial as a prefix */

        for (device_iter = 0; device_iter < device_count; device_iter++) {
            if (rtlsdr_get_device_usb_strings(device_iter, NULL, NULL, device_serial) < 0) continue;

            if (!strncmp(device_serial, user_serial, strlen(user_serial)) && !rtlsdr_open(out_rtldev, device_iter))
                goto found;
        }

        /* Search for user_serial as suffix */

        for (device_iter = 0; device_iter < device_count; device_iter++) {
            int offset = 0;

            if (rtlsdr_get_device_usb_strings(device_iter, NULL, NULL, device_serial) < 0) continue;
            offset = strnlen(device_serial, 255) - strlen(user_serial);

            if ( !(offset < 0) && !strncmp(device_serial+offset, user_serial, strlen(user_serial))
                    && !rtlsdr_open(out_rtldev, device_iter))
                goto found;
        }

        MFM_MSG(SEV_ERROR, "DEV-NOT-FOUND", "Unable to open any RTLSDR matching or containing configured deviceSerial '%s'.", user_serial);
	ret = A_E_NOTFOUND;
        goto done;
    }

    /* Attempt to open by index */
    if ( !(user_idx < 0) ) {
        device_iter = user_idx;
        if (!rtlsdr_open(out_rtldev, device_iter))
            goto found;

	MFM_MSG(SEV_ERROR, "DEV-NOT-FOUND", "Unable to open RTLSDR with configued deviceIndex '%d'.", user_idx);
        ret = A_E_NOTFOUND;
        goto done;
    }

    /* Open first available device. */
    for (device_iter = 0; device_iter < device_count; device_iter++) {
        if (!rtlsdr_open(out_rtldev, device_iter)) 
            goto found;
    }

    ret = A_E_NOTFOUND;
found:
    *out_idx = device_iter;

done:
    return ret;
}

/**
 * Create and start a new worker thread for the RTL-SDR.
 */
aresult_t rtl_sdr_worker_thread_new(
        struct receiver **pthr,
        struct config *cfg)
{
    aresult_t ret = A_OK;

    struct rtl_sdr_thread *thr = NULL;
    struct rtlsdr_dev *dev = NULL;
    struct config device = CONFIG_INIT_EMPTY;
    const char *rtl_dump_file = NULL;
    const char *dev_user_serial = NULL;
    char dev_serial[256] = { 0 };
    int dev_user_idx = -1,
        dev_idx = -1,
        rret = 0,
        ppm_corr = 0,
        rtl_ret = 0,
        dump_file_fd = -1,
        sample_rate = 0,
        center_freq = 0;
    bool test_mode = false;
    double if_gain_db = 0.0,
           gain_db = 1.0;
    enum rtlsdr_tuner tuner_type = RTLSDR_TUNER_UNKNOWN;


    TSL_ASSERT_ARG(NULL != cfg);
    TSL_ASSERT_ARG(NULL != pthr);

    *pthr = NULL;

    TSL_BUG_IF_FAILED(config_get(cfg, &device, "device"));

    if (FAILED(ret = config_get_integer(cfg, &sample_rate, "sampleRateHz"))) {
        MFM_MSG(SEV_INFO, "NO-SAMPLE-RATE", "Need to specify a sample rate, in Hertz.");
        goto done;
    }

    if (FAILED(ret = config_get_integer(cfg, &center_freq, "centerFreqHz"))) {
        MFM_MSG(SEV_INFO, "NO-CENTER-FREQ", "You forgot to specify a center frequency, in Hz.");
        goto done;
    }

    rret = config_get_integer(&device, &dev_user_idx, "deviceIndex");
    if (FAILED(rret)) {
        if (rret != A_E_NOTFOUND) {
            MFM_MSG(SEV_ERROR, "DEV-DEVIDX-INVAL", "Value for 'deviceIndex' is invalid.");
            ret = rret;
            goto done;
        }
    }

    rret = config_get_string(&device, &dev_user_serial, "deviceSerial");
    if (FAILED(rret)) {
        if (rret != A_E_NOTFOUND) {
            MFM_MSG(SEV_ERROR, "DEV-DEVSER-INVAL", "Value for 'deviceSerial' is invalid.");
            ret = rret;
            goto done;
        }
    }

    if (FAILED(ret = __rtl_sdr_device_search(dev_idx, dev_user_serial, &dev_idx, &dev))) {
        MFM_MSG(SEV_ERROR, "DEV-NOT-FOUND", "Unable to open device.");
        goto done;
    }

    TSL_BUG_ON(NULL == dev);

    tuner_type = rtlsdr_get_tuner_type(dev);

    rtlsdr_get_usb_strings(dev, NULL, NULL, dev_serial);

    MFM_MSG(SEV_INFO, "DEV-IDX-OPEN", "Successfully opened device %d with serial '%s'.", dev_idx, dev_serial);
    MFM_MSG(SEV_INFO, "DEV-IDX-OPEN", "Device: %s Tuner: %s", rtlsdr_get_device_name(dev_idx), __rtl_sdr_tuner_names[(unsigned)tuner_type]);

    if (RTLSDR_TUNER_E4000 != tuner_type && RTLSDR_TUNER_R820T != tuner_type) {
        MFM_MSG(SEV_WARNING, "DEV-UNTESTED", "This tuner type is not tested, so the performance could be poor");
    }

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
    if (!FAILED(config_get_float(&device, &gain_db, "dBGainLNA"))) {
        /* Set the receiver gain based on the value in config */
        TSL_BUG_IF_FAILED(__rtl_sdr_worker_set_gain(dev, gain_db * 10));
    } else {
        MFM_MSG(SEV_INFO, "AUTO-GAIN-CONTROL", "Enabling automatic gain control.");
        TSL_BUG_ON(0 != rtlsdr_set_tuner_gain_mode(dev, 0));
    }

    if (tuner_type == RTLSDR_TUNER_E4000) {
        if (!FAILED(config_get_float(&device, &if_gain_db, "dbGainIF"))) {
            TSL_BUG_IF_FAILED(__rtl_sdr_worker_e4000_set_if_gain(dev, if_gain_db * 10));
        }
    }

    DIAG("LNA gain set to: %f", (double)rtlsdr_get_tuner_gain(dev)/10.0);

    /* Set the PPM correction, if specified */
    if (FAILED(config_get_integer(&device, &ppm_corr, "ppmCorrection"))) {
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
    if (!FAILED(config_get_string(&device, &rtl_dump_file, "iqDumpFile"))) {
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

    /* For debugging purposes, enable test mode. */
    if (!FAILED(config_get_boolean(cfg, &test_mode, "sdrTestMode")) & (true == test_mode)) {
        MFM_MSG(SEV_INFO, "TEST-MODE", "Enabling RTL-SDR test mode");
        if (0 != rtlsdr_set_testmode(dev, 1)) {
            MFM_MSG(SEV_ERROR, "CANT-SET-TEST-MODE", "Failed to enable test mode, aborting.");
            ret = A_E_INVAL;
            goto done;
        }
    }

    /* Create the worker thread context */
    if (FAILED(TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        ret = A_E_NOMEM;
        goto done;
    }

    thr->dev = dev;
    thr->dump_fd = dump_file_fd;

    /* Initialize the worker thread */
    TSL_BUG_IF_FAILED(receiver_init(&thr->rx, cfg, _rtl_sdr_worker_thread, _rtl_sdr_worker_thread_delete,
                RTL_SDR_DEFAULT_NR_SAMPLES));

    /* The caller can start the thread at its leisure now */
    *pthr = &thr->rx;

done:
    if (FAILED(ret)) {
        if (0 <= dump_file_fd) {
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

