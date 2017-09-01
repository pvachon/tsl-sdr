#include <flexmitter/flexmitter.h>
#include <flexmitter/tx_thread.h>

#include <config/engine.h>

#include <tsl/worker_thread.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>

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
};

aresult_t tx_thread_new(struct tx_thread **pthr, struct config *thr_cfg)
{
    aresult_t ret = A_OK;

    int hackrf_ret = 0;
    const char *serial_num = NULL;
    int center_freq = -1;
    struct tx_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != thr_cfg);

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

    /* Set the center frequency */
    if (HACKRF_SUCCESS != (hackrf_ret = hackrf_set_freq(thr->dev, center_freq))) {
        FLX_MSG(SEV_FATAL, "HACKRF", "Failed to set transmit center frequency.");
        ret = A_E_INVAL;
        goto done;
    }

    FLX_MSG(SEV_INFO, "HACKRF", "Set transmit center frequency to %8.5f MHz",
            (double)center_freq/1e6);

    /* Set the sample output rate to 1 MHz */

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
    return -1;
}

static
aresult_t __tx_thread_work(struct worker_thread *wthr)
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
    if (FAILED(ret = worker_thread_new(&thr->thr, __tx_thread_work, WORKER_THREAD_CPU_MASK_ANY))) {
        goto done;
    }

done:
    return ret;
}

