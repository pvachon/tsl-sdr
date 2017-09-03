#include <flexmitter/flexmitter.h>
#include <flexmitter/tx_thread.h>

#include <filter/polyphase_fir.h>

#include <config/engine.h>

#include <tsl/worker_thread.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>
#include <tsl/work_queue.h>
#include <tsl/frame_alloc.h>

#include <libhackrf/hackrf.h>

/**
 * The transmit thread state
 */
struct tx_thread {
    /**
     * The current worker thread
     */
    struct worker_thread thr;

    /**
     * HackRF device context
     */
    struct hackrf_device *dev;

    /**
     * Work queue for raw sample buffers at 32kHz
     */
    struct work_queue *wq;

    /**
     * Frame allocator for sample buffers
     */
    struct frame_alloc *fa;

    /**
     * Polyphase resampler for going from 32kHz to 8 MHz
     */
    struct polyphase_fir *pfir;

    /**
     * Active frame
     */
    int16_t *cur_frame;

    /**
     * Position in active frame
     */
    size_t cur_frame_off;
};

aresult_t tx_thread_new(struct tx_thread **pthr, struct config *thr_cfg, struct work_queue *wq, struct frame_alloc *fa)
{
    aresult_t ret = A_OK;

    int hackrf_ret = 0,
        center_freq = -1,
        /* int_gain_if = 0, */
        int_gain_txvga = 0;
    const char *serial_num = NULL;
    struct tx_thread *thr = NULL;
    double /* tx_if_gain_db = 0.0, */
           tx_vga_gain_db = 0.0,
           ppm_clock_corr = 0.0,
           tx_freq_hz_corr = 0.0;
    double *lpf_coeffs = NULL;
    size_t nr_lpf_coeffs = 0;
    bool bias_tee = false;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != thr_cfg);
    TSL_ASSERT_ARG(NULL != wq);
    TSL_ASSERT_ARG(NULL != fa);

    if (FAILED(ret = TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    if (FAILED(ret = config_get_string(thr_cfg, &serial_num, "hackRfSerial"))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Need to specify HackRF device serial number.");
        goto done;
    }

    if (FAILED(ret = config_get_integer(thr_cfg, &center_freq, "centerFreq"))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to get center frequency, aborting.");
        goto done;
    }

#if 0
    config_get_float(thr_cfg, &tx_if_gain_db, "dBGainIf");
#endif

    config_get_float(thr_cfg, &tx_vga_gain_db, "dBGainTxVga");

    if (!FAILED(config_get_float(thr_cfg, &ppm_clock_corr, "ppmClockCorr"))) {
        FLX_MSG(SEV_INFO, "HACKRF", "Will set PPM clock correction to %f PPM", ppm_clock_corr);
    }

    config_get_boolean(thr_cfg, &bias_tee, "enableBiasTee");

    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_init())) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to initialize HackRF library, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    /* Open the HackRF by serial number */
    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_open_by_serial(serial_num, &thr->dev))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Could not open the specified HackRF '%s'",
                serial_num);
        ret = A_E_INVAL;
        goto done;
    }

    FLX_MSG(SEV_INFO, "HACKRF", "Opened device '%s' successfully!", serial_num);

    /* Adjust the transmit frequency per the PPM drift anticipated */
    tx_freq_hz_corr = (double)center_freq * (1.0 + (ppm_clock_corr * 1.0e-6));

    DIAG("Corrected transmit frequency, based on calibrated PPM drift: %.6f Hz", tx_freq_hz_corr);

    /* Set the center frequency */
    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_set_freq(thr->dev, tx_freq_hz_corr))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to set transmit center frequency.");
        ret = A_E_INVAL;
        goto done;
    }

    FLX_MSG(SEV_INFO, "HACKRF", "Set transmit center frequency to %8.5f MHz",
            (double)center_freq/1e6);

#if 0
    /* TODO: this always truncates, we should be a bit smarter on the round */
    int_gain_if = tx_if_gain_db;

    if (int_gain_if > 14) {
        int_gain_if = 14;
        FLX_MSG(SEV_WARNING, "HACKRF", "Specified IF gain %d is too large, clamping to 14 dB", int_gain_if);
    } else {
        int_gain_if = 0;
        FLX_MSG(SEV_WARNING, "HACKRF", "Negative IF gain values are not supported (i.e. attenuation), clamping to 0 dB");
    }

    DIAG("Setting transmit IF gain to %d dB", int_gain_if);

    /* Set the transmit gain */
    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_set_amp_enable(thr->dev, int_gain_if))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to set IF transmit gain to %d dB, aborting.", int_gain_if);
        ret = A_E_INVAL;
        goto done;
    }

    FLX_MSG(SEV_INFO, "HACKRF", "Set output IF amplifier gain to %d.0 dB", int_gain_if);
#endif

    int_gain_txvga = tx_vga_gain_db;
    if (int_gain_txvga > 47) {
        int_gain_txvga = 47;
        FLX_MSG(SEV_WARNING, "HACKRF", "Clipping TX VGA gain to 47 dB.");
    } else if (int_gain_txvga < 0) {
        int_gain_txvga = 0;
        FLX_MSG(SEV_WARNING, "HACKRF", "TX VGA gain must be a postitive integer. Clipping to 0.");
    }

    DIAG("Setting transmit VGA gain to %d dB", int_gain_txvga);
    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_set_txvga_gain(thr->dev, int_gain_txvga))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to set TX VGA gaint to %d dB, aborting.", int_gain_txvga);
        ret = A_E_INVAL;
        goto done;
    }

    FLX_MSG(SEV_INFO, "HACKRF", "Set TX VGA gain to %d dB", int_gain_txvga);

    /* Set the sample output rate to 8 MHz */
    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_set_sample_rate_manual(thr->dev, 16000000ul, 2))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to set transmit sample rate to 8 MHz, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    DIAG("Successfully set sample rate to 8MHz");

    if (true == bias_tee) {
        /* TODO: keeping disabled for development purposes. */
        FLX_MSG(SEV_INFO, "HACKRF", "Enabled Bias Tee!");
    }

    thr->wq = wq;
    thr->fa = fa;

    /* And we're set! */
    *pthr = thr;

done:
    if (FAILED(ret)) {
        if (NULL != thr) {
            if (NULL != thr->dev) {
                hackrf_close(thr->dev);
                thr->dev = NULL;
            }
            TFREE(thr);
        }
    }
    return ret;
}

aresult_t tx_thread_delete(struct tx_thread **pthr)
{
    aresult_t ret = A_OK;

    struct tx_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != *pthr);

    thr = *pthr;

    TSL_BUG_IF_FAILED(worker_thread_request_shutdown(&thr->thr));

    if (NULL != thr->dev) {
        hackrf_close(thr->dev);
    }

    TFREE(thr);

    *pthr = thr = NULL;

    return ret;
}

static
int __tx_thread_tx(hackrf_transfer *tx)
{
    int ret = 0;
    int16_t *sbuf = NULL;
    struct tx_thread *thr = tx->tx_ctx;

    TSL_BUG_ON(NULL == thr);

    /* Pop an item out of the work queue */
    if (FAILED(work_queue_pop(thr->wq, (void **)&sbuf))) {
        DIAG("Nothing available for transmitting!");
        goto done;
    }

    /* Convert from 32kHz to 8MHz */

    /* Arm the transfer */

    /* Dispatch! */
done:
    if (NULL != sbuf) {
        TSL_BUG_ON(NULL == thr);
        TSL_BUG_IF_FAILED(frame_free(thr->fa, (void **)&sbuf));
    }
    return ret;
}

static
aresult_t _tx_thread_work(struct worker_thread *wthr)
{
    aresult_t ret = A_OK;

    struct tx_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != wthr);

    thr = BL_CONTAINER_OF(wthr, struct tx_thread, thr);

    hackrf_start_tx(thr->dev, __tx_thread_tx, thr);

    do {
        sleep(1);
    } while (worker_thread_is_running(wthr));

    hackrf_stop_tx(thr->dev);

    return ret;
}

aresult_t tx_thread_start(struct tx_thread *thr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != thr);

    /* Create the transmit worker thread */
    if (FAILED(ret = worker_thread_new(&thr->thr, _tx_thread_work, WORKER_THREAD_CPU_MASK_ANY))) {
        goto done;
    }

done:
    return ret;
}

