#include <multifm/uhd_if.h>
#include <multifm/uhd_if_priv.h>
#include <multifm/receiver.h>

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <tsl/safe_alloc.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <string.h>

#define UHD_FAILED(x) (!!((x) != UHD_ERROR_NONE))

#define MAX_BUF_SAMPS   (16 * 1024)

static
aresult_t _uhd_rx_worker_thread(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct uhd_worker_thread *uw = NULL;
    uhd_rx_metadata_handle meta = NULL;
    struct sample_buf *buf = NULL;
    uhd_stream_cmd_t sc;

    TSL_ASSERT_ARG(NULL != rx);

    if (UHD_FAILED(uhd_rx_metadata_make(&meta))) {
        UHD_MSG(SEV_FATAL, "OUT-OF-MEM", "Could not make UHD RX Metadata object, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    sc = (uhd_stream_cmd_t){
        .stream_mode = UHD_STREAM_MODE_START_CONTINUOUS,
        .stream_now = true,
    };

    uw = BL_CONTAINER_OF(rx, struct uhd_worker_thread, rx);

    if (UHD_FAILED(uhd_rx_streamer_issue_stream_cmd(uw->rx_stream, &sc))) {
        UHD_MSG(SEV_FATAL, "FAILED-TO-START", "Failed to issue stream command to USRP, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    while (receiver_thread_running(rx)) {
        size_t nr_samps = 0;
        TSL_BUG_IF_FAILED(receiver_sample_buf_alloc(rx, &buf));
        buf->nr_samples = 0;

        while (NULL != buf) {
            void *buf_offs = buf->data_buf + 2 * sizeof(int16_t) * buf->nr_samples;
            uhd_rx_metadata_error_code_t error_code;

            if (UHD_FAILED(uhd_rx_streamer_recv(uw->rx_stream, &buf_offs, MAX_BUF_SAMPS - buf->nr_samples,
                            &meta, 5.0, false, &nr_samps)))
            {
                UHD_MSG(SEV_FATAL, "RECEIVE-ERROR", "Failure while receiving USRP samples, aborting.");
                ret = A_E_INVAL;
                goto done;
            }

            if (UHD_FAILED(uhd_rx_metadata_error_code(meta, &error_code))) {
                UHD_MSG(SEV_FATAL, "RX-ERROR", "Receive error occurred.");
                ret = A_E_INVAL;
                goto done;
            }

            if (error_code != UHD_RX_METADATA_ERROR_CODE_NONE) {
                UHD_MSG(SEV_FATAL, "RX-ERROR-CODE", "Receive error code received: %d", error_code);
                ret = A_E_INVAL;
                goto done;
            }

            buf->nr_samples += nr_samps;

            if (buf->nr_samples == MAX_BUF_SAMPS) {
                TSL_BUG_IF_FAILED(receiver_sample_buf_deliver(rx, buf));
                buf = NULL;
            }
        } /* end iterating to fill buffer */
    } /* end infinite loop while thread is running */

done:
    if (NULL != meta) {
        uhd_rx_metadata_free(&meta);
    }

    return ret;
}

static
aresult_t _uhd_cleanup(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct uhd_worker_thread *uw = NULL;

    TSL_ASSERT_ARG(NULL != rx);

    uw = BL_CONTAINER_OF(rx, struct uhd_worker_thread, rx);

    if (NULL != uw->rx_stream) {
        if (UHD_FAILED(uhd_rx_streamer_free(&uw->rx_stream))) {
            UHD_MSG(SEV_FATAL, "CLEANUP-FAIL", "Failed to release RX streamer handle, aborting.");
            ret = A_E_INVAL;
            goto done;
        }

        uw->rx_stream = NULL;
    }

    if (NULL != uw->dev_hdl) {
        if (UHD_FAILED(uhd_usrp_free(&uw->dev_hdl))) {
            UHD_MSG(SEV_FATAL, "CLEANUP-FAIL", "Failed to release USRP handle, aborting.");
            ret = A_E_INVAL;
            goto done;
        }

        uw->dev_hdl = NULL;
    }

done:
    return ret;
}

static
aresult_t _uhd_set_channel_gain(struct uhd_worker_thread *uthr, size_t channel, const char *gain_name, double gain_db)
{
    aresult_t ret = A_OK;

    double set_gain = 0.0;

    TSL_ASSERT_ARG(NULL != uthr);
    TSL_ASSERT_ARG(0.0 <= gain_db);

    if (UHD_FAILED(uhd_usrp_set_rx_gain(uthr->dev_hdl, gain_db, channel, gain_name))) {
        UHD_MSG(SEV_FATAL, "FAILED-TO-SET-GAIN", "Failed to set RX gain '%s' for channel %zu to %f",
                gain_name, channel, gain_db);
        ret = A_E_INVAL;
        goto done;
    }

    if (UHD_FAILED(uhd_usrp_get_rx_gain(uthr->dev_hdl, channel, gain_name, &set_gain ))) {
        UHD_MSG(SEV_FATAL, "FAILED-TO-GET-GAIN", "Failed to get RX gain '%s' for channel %zu",
                gain_name, channel);
        ret = A_E_INVAL;
        goto done;
    }

    UHD_MSG(SEV_INFO, "CHANNEL-GAIN", "Gain [%s] on channel %zu: %f dB (requested %f dB)", gain_name, channel,
            set_gain, gain_db);

done:
    return ret;
}

static
aresult_t _uhd_tune_channel(struct uhd_worker_thread *uthr, size_t channel, uint64_t rx_rate, uint64_t freq_hz)
{
    aresult_t ret = A_OK;

    uhd_tune_request_t tune_req;
    uhd_tune_result_t tune_result;

    double d_rate = rx_rate,
           d_freq = freq_hz;

    TSL_ASSERT_ARG(NULL != uthr);
    TSL_ASSERT_ARG(0 != freq_hz);

    memset(&tune_req, 0, sizeof(tune_req));

    TSL_BUG_ON(NULL == uthr->dev_hdl);

    DIAG("Setting sampling rate to %zu Hz", rx_rate);

    if (UHD_FAILED(uhd_usrp_set_rx_rate(uthr->dev_hdl, d_rate, channel))) {
        ret = A_E_INVAL;
        UHD_MSG(SEV_FATAL, "FAILED-SET-RX-RATE", "Failed to set receive rate, aborting.");
        goto done;
    }

    if (UHD_FAILED(uhd_usrp_get_rx_rate(uthr->dev_hdl, channel, &d_rate))) {
        ret = A_E_INVAL;
        UHD_MSG(SEV_FATAL, "FAILED-GET-RX-RATE", "Failed to retrieve receive rate, aborting.");
        goto done;
    }

    UHD_MSG(SEV_INFO, "RX-RATE", "Requested RX rate: %zu Hz, got %zu Hz", rx_rate, (uint64_t)d_rate);

    DIAG("Tuning radio to %zu Hz on channel %zu", freq_hz, channel);

    tune_req = (uhd_tune_request_t){
        .target_freq = d_freq,
        .rf_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
        .dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO,
    };

    if (UHD_FAILED(uhd_usrp_set_rx_freq(uthr->dev_hdl, &tune_req, channel, &tune_result))) {
        ret = A_E_INVAL;
        UHD_MSG(SEV_FATAL, "FAILED-TO-SET-CENTER-FREQ", "Could not set center frequency to %f Hz, aborting",
                d_freq);
        goto done;
    }

    TSL_BUG_ON(UHD_FAILED(uhd_usrp_get_rx_freq(uthr->dev_hdl, channel, &d_freq)));

    UHD_MSG(SEV_INFO, "RX-TUNING", "Requested center frequency %zu Hz, got %zu Hz", freq_hz, (uint64_t)d_freq);

done:
    return ret;
}

static
aresult_t _uhd_dump_antenna_names(struct uhd_worker_thread *uthr, size_t channel)
{
    aresult_t ret = A_OK;

    uhd_string_vector_handle names = NULL;
    char str[128];
    size_t ct = 0;

    TSL_ASSERT_ARG(NULL != uthr);
    TSL_BUG_ON(NULL == uthr->dev_hdl);

    DIAG("Getting gains for channel %zu...", channel);

    if (UHD_FAILED(uhd_string_vector_make(&names))) {
        UHD_MSG(SEV_FATAL, "OUT-OF-MEMORY", "Could not make string vector, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    if (UHD_FAILED(uhd_usrp_get_rx_antennas(uthr->dev_hdl, channel, &names))) {
        UHD_MSG(SEV_INFO, "CANNOT-GET-ANTENNAS", "Could not get list of antenna names from device, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    TSL_BUG_ON(UHD_FAILED(uhd_string_vector_size(names, &ct)));

    UHD_MSG(SEV_INFO, "ANTENNAS", "Available antennas on channel %zu:", channel);

    for (size_t i = 0; i < ct; i++) {
        TSL_BUG_ON(UHD_FAILED(uhd_string_vector_at(names, i, str, sizeof(ct) - 1)));
        UHD_MSG(SEV_INFO, "ANTENNAS", "    ->  %s", str);
    }

done:
    if (NULL != names) {
        TSL_BUG_ON(UHD_FAILED(uhd_string_vector_free(&names)));
        names = NULL;
    }
    return ret;
}

static
aresult_t _uhd_dump_gain_names(struct uhd_worker_thread *uthr, size_t channel)
{
    aresult_t ret = A_OK;

    uhd_string_vector_handle names = NULL;
    char str[128];
    size_t ct = 0;

    TSL_ASSERT_ARG(NULL != uthr);
    TSL_BUG_ON(NULL == uthr->dev_hdl);

    DIAG("Getting gains for channel %zu...", channel);

    if (UHD_FAILED(uhd_string_vector_make(&names))) {
        UHD_MSG(SEV_FATAL, "OUT-OF-MEMORY", "Could not make string vector, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    if (UHD_FAILED(uhd_usrp_get_rx_gain_names(uthr->dev_hdl, channel, &names))) {
        UHD_MSG(SEV_INFO, "CANNOT-GET-GAINS", "Could not get list of gain names from device, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    DIAG("Getting vector size for %p...", names);

    TSL_BUG_ON(UHD_FAILED(uhd_string_vector_size(names, &ct)));

    UHD_MSG(SEV_INFO, "GAINS", "Available gains on channel %zu:", channel);

    for (size_t i = 0; i < ct; i++) {
        TSL_BUG_ON(UHD_FAILED(uhd_string_vector_at(names, i, str, sizeof(ct) - 1)));
        UHD_MSG(SEV_INFO, "GAINS", "   ->  %s", str);
    }

done:
    if (NULL != names) {
        TSL_BUG_ON(UHD_FAILED(uhd_string_vector_free(&names)));
        names = NULL;
    }
    return ret;
}

aresult_t uhd_worker_thread_new(struct receiver **pthr, struct config *cfg)
{
    aresult_t ret = A_OK;

    struct uhd_worker_thread *uthr = NULL;

    const char *dev_str = NULL,
               *antenna = NULL;
    struct config device = CONFIG_INIT_EMPTY,
                  gains = CONFIG_INIT_EMPTY,
                  gain_config = CONFIG_INIT_EMPTY;
    uhd_stream_args_t sa;
    int channel = 0,
        sample_rate = 0,
        center_freq = 0;
    size_t chan_t = 0,
           cnt = 0,
           samps_per_buf = 0;
    char str[128];

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != cfg);

    *pthr = NULL;

    memset(&sa, 0, sizeof(sa));

    /* Grab the config */
    if (FAILED(ret = config_get(cfg, &device, "device"))) {
        UHD_MSG(SEV_FATAL, "MISSING-DEVICE", "Missing device specification stanza, aborting.");
        goto done;
    }

    if (FAILED(ret = config_get_string(&device, &dev_str, "deviceId"))) {
        UHD_MSG(SEV_FATAL, "MISSING-DEVICE-ID", "Need to specify deviceId in device stanza, aborting.");
        goto done;
    }

    if (FAILED(config_get_integer(&device, &channel, "channelId"))) {
        UHD_MSG(SEV_INFO, "DEFAULT-CHANNEL", "No receive channel specified, defaulting to 0");
        channel = 0;
    }

    DIAG("Device ID: [%s] Channel: %d", dev_str, channel);

    if (FAILED(ret = config_get_integer(cfg, &sample_rate, "sampleRateHz"))) {
        UHD_MSG(SEV_FATAL, "NO-SAMPLE-RATE", "Need to specify sampleRateHz in configuration");
        goto done;
    }

    if (FAILED(ret = config_get_integer(cfg, &center_freq, "centerFreqHz"))) {
        UHD_MSG(SEV_FATAL, "NO-CENTER-FREQ", "Need to specify centerFreqHz in configuration");
        goto done;
    }

    /* Create our state structure */
    if (FAILED(ret = TZAALLOC(uthr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    /* Open the USRP, create the RX handle */
    if (UHD_FAILED(uhd_usrp_make(&uthr->dev_hdl, dev_str))) {
        UHD_MSG(SEV_FATAL, "FAILED-CREATION", "Failed to create USRP device, with configuration string %s",
                dev_str);
        ret = A_E_INVAL;
        goto done;
    }

    if (!UHD_FAILED(uhd_usrp_get_rx_antenna(uthr->dev_hdl, channel, str, sizeof(str) - 1))) {
        DIAG("Channel %d label: [%s]", channel, str);
    }

    if (FAILED(ret = config_get_string(&device, &antenna, "antenna"))) {
        UHD_MSG(SEV_FATAL, "NO-ANTENNA", "Need to specify an antenna, aborting");
        TSL_BUG_IF_FAILED(_uhd_dump_antenna_names(uthr, channel));
        goto done;
    }

    UHD_MSG(SEV_INFO, "OPENED-DEVICE", "Opened USRP [%s] Channel: %d", dev_str, channel);

    /* Set the input antenna */
    if (UHD_FAILED(uhd_usrp_set_rx_antenna(uthr->dev_hdl, antenna, channel))) {
        UHD_MSG(SEV_FATAL, "NO-RX-ANTENNA", "Failed to set channel %d to input from antenna %s",
                channel, antenna);
        ret = A_E_INVAL;
        goto done;
    }

    /* Prepare the RX streamer */
    if (UHD_FAILED(uhd_rx_streamer_make(&uthr->rx_stream))) {
        UHD_MSG(SEV_FATAL, "FAILED-STREAM-CREATION", "Failed to create RX streamer, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Tune the front-end to the specified center frequency */
    if (FAILED(ret = _uhd_tune_channel(uthr, channel, sample_rate, center_freq))) {
        goto done;
    }

    /* Set up the gain(s) */
    if (FAILED(ret = config_get(&device, &gains, "gain"))) {
        UHD_MSG(SEV_FATAL, "NO-GAINS", "No gains have been specified for channel %d", channel);
        TSL_BUG_IF_FAILED(_uhd_dump_gain_names(uthr, channel));
        goto done;
    }

    CONFIG_ARRAY_FOR_EACH(gain_config, &gains, ret, cnt) {
        const char *name = NULL;
        double value = 0.0;

        if (FAILED(ret = config_get_string(&gain_config, &name, "name"))) {
            UHD_MSG(SEV_FATAL, "MALFORMED-GAIN", "Gain configuration %zu is missing channel name, aborting.",
                    cnt);
            goto done;
        }

        if (FAILED(ret = config_get_float(&gain_config, &value, "dBValue"))) {
            UHD_MSG(SEV_FATAL, "MALFORMED-GAIN-VALUE", "Gain configuration is missing value at offset %zu",
                    cnt);
            goto done;
        }

        if (FAILED(ret = _uhd_set_channel_gain(uthr, channel, name, value))) {
            goto done;
        }
    }

    /* Get the RX stream */
    chan_t = channel;
    sa.cpu_format = "sc16";
    sa.otw_format = "sc16";
    sa.n_channels = 1;
    sa.channel_list = &chan_t;
    sa.args = "";

    if (UHD_FAILED(uhd_usrp_get_rx_stream(uthr->dev_hdl, &sa, uthr->rx_stream))) {
        UHD_MSG(SEV_FATAL, "FAILED-GET-RX-STREAM", "Failed to get rx stream for specified USRP device.");
        ret = A_E_INVAL;
        goto done;
    }

    if (UHD_FAILED(uhd_rx_streamer_max_num_samps(uthr->rx_stream, &samps_per_buf))) {
        UHD_MSG(SEV_FATAL, "FAILED-TO-GET-ATTRIB", "Failed to get the maximum samples per buffer, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    UHD_MSG(SEV_INFO, "SAMPLES-PER-BUFFER", "Maximum samples per buffer: %zu", samps_per_buf);

    /* Initialize the receiver subsystem */
    DIAG("Initializing the receiver subsystem.");
    TSL_BUG_IF_FAILED(receiver_init(&uthr->rx, cfg, _uhd_rx_worker_thread, _uhd_cleanup, MAX_BUF_SAMPS));

    DIAG("We're all set up!");

    *pthr = &uthr->rx;

done:
    if (FAILED(ret)) {
        if (NULL != uthr) {
            if (NULL != uthr->rx_stream) {
                uhd_rx_streamer_free(&uthr->rx_stream);
                uthr->rx_stream = NULL;
            }
            if (NULL != uthr->dev_hdl) {
                uhd_usrp_free(&uthr->dev_hdl);
                uthr->dev_hdl = NULL;
            }
            TFREE(uthr);
        }

    }

    return ret;
}

