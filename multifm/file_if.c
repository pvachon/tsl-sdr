#include <multifm/file_if.h>
#include <multifm/file_if_priv.h>
#include <multifm/receiver.h>

#include <config/engine.h>

#include <filter/sample_buf.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define SAMPLES_PER_BUF     (4 * 1024)

static
aresult_t __file_read_bytes(struct file_worker_thread *rx, void *tgt_buf, size_t max_bytes, size_t *pbytes_read)
{
    aresult_t ret = A_OK;

    long nr_bytes = 0;

    TSL_ASSERT_ARG(NULL != rx);
    TSL_ASSERT_ARG(NULL != tgt_buf);
    TSL_ASSERT_ARG(0 != max_bytes);
    TSL_ASSERT_ARG(NULL != pbytes_read);

    if (0 > (nr_bytes = read(rx->fd, tgt_buf, max_bytes))) {
        int errnum = errno;
        FL_MSG(SEV_FATAL, "FILE-READ-ERROR", "Failed to read data from file, reason: %s (%d)",
                strerror(errnum), errnum);
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

static
aresult_t _file_read_cs16(struct file_worker_thread *rx, struct sample_buf *sbuf)
{
    aresult_t ret = A_OK;

    size_t nr_read = 0;

    TSL_ASSERT_ARG(NULL != rx);
    TSL_ASSERT_ARG(NULL != sbuf);

    if (FAILED(ret = __file_read_bytes(rx, sbuf->data_buf, SAMPLES_PER_BUF * 2 * sizeof(int16_t), &nr_read))) {
        goto done;
    }

    sbuf->nr_samples = nr_read/(2 * sizeof(int16_t));

done:
    return ret;
}

static
aresult_t _file_read_cs8(struct file_worker_thread *rx, struct sample_buf *sbuf)
{
    aresult_t ret = A_OK;

    size_t nr_read = 0,
           rem = 0;

    int8_t *in_buf = NULL;
    int16_t *out_buf = NULL;

    TSL_ASSERT_ARG(NULL != rx);
    TSL_ASSERT_ARG(NULL != sbuf);

    /* Read into bounce buffer */
    if (FAILED(ret = __file_read_bytes(rx, rx->bounce_buf, rx->bounce_buf_bytes, &nr_read))) {
        goto done;
    }

    TSL_BUG_ON(nr_read > rx->bounce_buf_bytes);

    in_buf = rx->bounce_buf;
    out_buf = (int16_t *)sbuf->data_buf;

    /* Convert to s16 just through a cast */
    for (size_t i = 0; i < nr_read; i += 4) {
        out_buf[i] = in_buf[i];
        out_buf[i + 1] = in_buf[i + 1];
        out_buf[i + 2] = in_buf[i + 2];
        out_buf[i + 3] = in_buf[i + 3];
    }

    rem = nr_read % 4;

    for (size_t i = 0; i < rem; i++) {
        out_buf[nr_read - rem + i] = in_buf[nr_read - rem + i];
    }

    /* Ensure we mark the buffer only for the number of samples actually available */
    sbuf->nr_samples = nr_read/2;

done:
    return ret;
}

static
aresult_t _file_worker_thread_work(struct receiver *rx)
{
    aresult_t ret = A_OK;

    struct file_worker_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != rx);

    thr = BL_CONTAINER_OF(rx, struct file_worker_thread, rcvr);

    while (receiver_thread_running(rx)) {
        /* Start timer */
        uint64_t start_time = tsl_get_clock_monotonic();

        /* Read in a sample buffer */
        struct sample_buf *sbuf = NULL;
        TSL_BUG_IF_FAILED(receiver_sample_buf_alloc(rx, &sbuf));

        TSL_BUG_ON(NULL == thr->read_call);
        if (FAILED(ret = thr->read_call(thr, sbuf))) {
            /* Chances are we ran out of samples to process */
            goto done;
        }

        /* Deliver the sample buffer */
        TSL_BUG_IF_FAILED(receiver_sample_buf_deliver(rx, sbuf));

        /* Stop timer, check how long this mess took */
        uint64_t total_time = tsl_get_clock_monotonic() - start_time;

        /* Sleep until we should deliver the next one */
        if (total_time < thr->time_per_buf_ns) {
            usleep((thr->time_per_buf_ns - total_time)/1000);
        }
    }

done:
    return ret;
}

static
aresult_t _file_worker_thread_cleanup(struct receiver *rx)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != rx);

    return ret;
}

aresult_t file_worker_thread_new(struct receiver **pthr, struct config *cfg)
{
    aresult_t ret = A_OK;

    struct file_worker_thread *thr = NULL;
    int fd = -1;
    const char *filename = NULL,
               *format = NULL;
    struct config devcfg = CONFIG_INIT_EMPTY;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != cfg);

    if (FAILED(ret = config_get(cfg, &devcfg, "device"))) {
        FL_MSG(SEV_FATAL, "MISSING-DEVICE-STANZA", "Missing 'device' stanza of configuration, aborting.");
        goto done;
    }

    if (FAILED(ret = config_get_string(&devcfg, &filename, "filename"))) {
        FL_MSG(SEV_FATAL, "CONFIG-NO-FILE", "Need to specify a filename in the device config, aborting.");
        goto done;
    }

    if (FAILED(config_get_string(&devcfg, &format, "fileFormat"))) {
        goto done;
    }

    /* Validate that the format is supported */
    if (!(strncmp(format, "cs16", 4) || strncmp(format, "cs8", 3))) {
        FL_MSG(SEV_FATAL, "UNSUPPORTED-FILE-FORMAT", "File format [%s] is not supported, aborting.",
                format);
        ret = A_E_INVAL;
        goto done;
    }

    FL_MSG(SEV_INFO, "CREATING-FILE-SOURCE", "Sourcing samples in format %s from file [%s]",
            format, filename);

    /* Try to open the file */
    if (0 > (fd = open(filename, O_RDONLY))) {
        int errnum = errno;
        FL_MSG(SEV_FATAL, "BAD-FILE", "Unable to open file [%s], aborting.", filename);
        ret = A_E_INVAL;
        goto done;
    }

    if (FAILED(ret = TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    thr->fd = fd;

    if (!strncmp(format, "cs8", 3)) {
        DIAG("Creating bounce buffer, cs8 format requires conversion.");
        thr->read_call = _file_read_cs8;

        if (FAILED(ret = TACALLOC(&thr->bounce_buf, SAMPLES_PER_BUF, 2 * sizeof(int8_t), SYS_CACHE_LINE_LENGTH))) {
            goto done;
        }

        thr->bounce_buf_bytes = SAMPLES_PER_BUF * 2 * sizeof(int8_t);
    } else if (!strncmp(format, "cs16", 4)) {
        thr->read_call = _file_read_cs16;
    } else {
        FL_MSG(SEV_FATAL, "UNSUPPORTED-SAMPLE-FORMAT", "Sample format [%s] is not supported, aborting.", format);
    }

    /* Initialize the receiver subsystem */
    TSL_BUG_IF_FAILED(receiver_init(&thr->rcvr, cfg, _file_worker_thread_work,
                _file_worker_thread_cleanup, SAMPLES_PER_BUF));

    *pthr = &thr->rcvr;

done:
    if (FAILED(ret)) {
        if (NULL != thr) {
            TFREE(thr);
            thr = NULL;
        }

        if (fd != -1) {
            close(fd);
            fd = -1;
        }
    }
    return ret;
}

