#include <multifm/airspy_if.h>
#include <multifm/receiver.h>
#include <multifm/multifm.h>

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <libdespairspy/airspy.h>

#include <string.h>

static
aresult_t _airspy_worker_thread_delete(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct airspy_thread *athr = NULL;

    TSL_ASSERT_ARG(NULL != rx);

    athr = BL_CONTAINER_OF(rx, struct airspy_thread, rx);

    if (NULL == athr->dev) {
        goto done;
    }

    if (0 != airspy_term_rx(athr->dev)) {
        ret = A_E_INVAL;
        goto done;
    }

    airspy_close(athr->dev);
    athr->dev = NULL;

done:
    return ret;
}

static
int _airspy_on_sample_block(struct airspy_device *dev, void *ctx, airspy_transfer *transfer)
{
    int ret = 0;

    struct airspy_thread *thr = ctx;
    struct sample_buf *sbuf = NULL;

    if (true == thr->rx.muted) {
        /* Don't do anything, we're muted */
        goto done;
    }

    /* TODO: Dump FD stuff */

    if (FAILED(receiver_sample_buf_alloc(&thr->rx, &sbuf))) {
        DIAG("Dropping buffer due to sample buffer memory pressure.");
        thr->dropped++;
        goto done;
    }

    DIAG("Received %u samples", transfer->sample_count);

    /* TODO: for now, just memcpy to the output */
    memcpy(sbuf->data_buf, transfer->samples, transfer->sample_count * sizeof(int16_t) * 2);
    sbuf->nr_samples = transfer->sample_count;

    /* Something has gone very wrong... */
    if (FAILED(receiver_sample_buf_deliver(&thr->rx, sbuf))) {
        TSL_BUG_IF_FAILED(sample_buf_decref(sbuf));
        ret = -1;
        goto done;
    }

done:
    return ret;
}

static
aresult_t _airspy_worker_thread(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct airspy_thread *athr = NULL;

    TSL_ASSERT_ARG(NULL != rx);

    athr = BL_CONTAINER_OF(rx, struct airspy_thread, rx);

    DIAG("Starting Airspy worker thread...");

    if (0 != airspy_init_rx(athr->dev)) {
        MFM_MSG(SEV_ERROR, "AIRSPY-ERROR", "Failed to initialize the receive path, aborting.");
        goto done;
    }

    if (0 != airspy_do_rx(athr->dev, _airspy_on_sample_block, athr)) {
        MFM_MSG(SEV_ERROR, "AIRSPY-ERROR", "Error while running Airspy capture process, aborting.");
    }

    if (0 != airspy_term_rx(athr->dev)) {
        MFM_MSG(SEV_ERROR, "AIRSPY-ERROR", "An error occurred while cleaning up Airspy, aborting.");
    }

    MFM_MSG(SEV_INFO, "AIRSPY-TERMINATED", "Terminating Airspy receiver thread");

done:
    return ret;
}

aresult_t airspy_worker_thread_new(struct receiver **pthr, struct config *cfg)
{
    aresult_t ret = A_OK;

    struct airspy_thread *athr = NULL;
    struct airspy_device *dev = NULL;
    int sample_rate = 0,
        center_freq = 0,
        ser_no = -1,
        lna_gain = 1,
        vga_gain = 5,
        mixer_gain = 5,
        airspy_ret = 0;
    bool bias_t = false;
    struct config device = CONFIG_INIT_EMPTY;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != cfg);

    TSL_BUG_IF_FAILED(config_get(cfg, &device, "device"));

    if (FAILED(ret = config_get_integer(cfg, &sample_rate, "sampleRateHz"))) {
        MFM_MSG(SEV_INFO, "NO-SAMPLE-RATE", "Need to specify a sample rate, in Hertz.");
        goto done;
    }

    if (FAILED(ret = config_get_integer(cfg, &center_freq, "centerFreqHz"))) {
        MFM_MSG(SEV_INFO, "NO-CENTER-FREQ", "You forgot to specify a center frequency, in Hz.");
        goto done;
    }

    /* Grab our device serial number, if present */
    if (FAILED(config_get_integer(&device, &ser_no, "serialNo"))) {
        /* Not present, so we'll open the first device we find */
        ser_no = -1;
    }

    /* Get the LNA gain from the device config */
    if (FAILED(config_get_integer(&device, &lna_gain, "lnaGain"))) {
        lna_gain = 1;
    }

    /* Get the VGA gain from the device config */
    if (FAILED(config_get_integer(&device, &vga_gain, "vgaGain"))) {
        vga_gain = 5;
    }

    /* Get the Mixer gain from the device config */
    if (FAILED(config_get_integer(&device, &mixer_gain, "mixerGain"))) {
        mixer_gain = 5;
    }

    MFM_MSG(SEV_INFO, "GAINS", "Gains: LNA = %d dB, VGA = %d dB, Mixer = %d dB",
            lna_gain, vga_gain, mixer_gain);

    /* Check if we should enable the Bias Tee */
    if (FAILED(config_get_boolean(&device, &bias_t, "enableBiasTee"))) {
        bias_t = false;
    } else {
        if (true == bias_t) {
            MFM_MSG(SEV_INFO, "BIAS-TEE", "Bias Tee is enabled, so hope you have something attached.");
        }
    }

    /* Open the device */
    if (-1 != ser_no) {
        if (0 != (airspy_ret = airspy_open_sn(&dev, ser_no))) {
            MFM_MSG(SEV_FATAL, "BAD-DEVICE", "Unable to find Airspy device with ID %d", ser_no);
            ret = A_E_INVAL;
            goto done;
        }
    } else {
        if (0 != (airspy_ret = airspy_open(&dev))) {
            MFM_MSG(SEV_FATAL, "NO-DEVICE", "Unable to find any Airspy devices.");
            ret = A_E_INVAL;
            goto done;
        }
    }

    /* Enable packed sample transfers. */
    if (0 != airspy_set_packing(dev, 1)) {
	MFM_MSG(SEV_WARNING, "FAILED-BIT-PACKING", "Request for packed sample transfers failed, continuing.");
    }

    /* Set the sample rate, as requested */
    if (0 != airspy_set_samplerate(dev, sample_rate)) {
        MFM_MSG(SEV_FATAL, "BAD-SAMPLE-RATE", "Unable to set sampling rate to %d Hz, aborting.",
                sample_rate);
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the center frequency to the requested value */
    if (0 != airspy_set_freq(dev, center_freq)) {
        MFM_MSG(SEV_FATAL, "BAD-CENTER-FREQ", "Unable to set center frequency to %d Hz, aborting.",
                center_freq);
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the LNA gain */
    if (0 != airspy_set_lna_gain(dev, lna_gain)) {
        MFM_MSG(SEV_FATAL, "BAD-LNA-GAIN", "LNA gain setting %d is invalid, aborting", lna_gain);
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the VGA gain */
    if (0 != airspy_set_vga_gain(dev, vga_gain)) {
        MFM_MSG(SEV_FATAL, "BAD-VGA-GAIN", "VGA gain setting %d is invalid, aborting", vga_gain);
        ret = A_E_INVAL;
        goto done;
    }

    /* Set the Mixer gain */
    if (0 != airspy_set_mixer_gain(dev, mixer_gain)) {
        MFM_MSG(SEV_FATAL, "BAD-MIXER-GAIN", "Mixer gain setting %d is invalid, aborting", mixer_gain);
        ret = A_E_INVAL;
        goto done;
    }

    /* Enable the Bias Tee if we were asked to do so */
    if (0 != airspy_set_rf_bias(dev, (true == bias_t) ? 1 : 0)) {
        MFM_MSG(SEV_WARNING, "FAILED-ENABLE-BIAS", "Failed to %sable Bias Tee for powering an outside device.",
		(true == bias_t) ? "en" : "dis");
    }

    /* Create the device object */
    if (FAILED(ret = TZAALLOC(athr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    athr->dev = dev;
    athr->dump_fd = -1;

    /* Initialize the worker thread */
    TSL_BUG_IF_FAILED(receiver_init(&athr->rx, cfg, _airspy_worker_thread, _airspy_worker_thread_delete,
                128 * 1024 * 2));

    *pthr = &athr->rx;

done:
    if (FAILED(ret)) {
        /* Clean up the device handle */
        if (NULL != dev) {
            airspy_close(dev);
        }
    }

    return ret;
}

