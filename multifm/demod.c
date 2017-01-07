#include <multifm/demod.h>
#include <multifm/direct_fir.h>
#include <multifm/sambuf.h>
#include <multifm/multifm.h>

#include <tsl/frame_alloc.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

#include <math.h>
#include <complex.h>
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef _USE_ARM_NEON
#include <arm_neon.h>
#endif

#define Q_15_SHIFT                      14

aresult_t sample_buf_decref(struct demod_thread *thr, struct sample_buf *buf)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != thr);
    TSL_ASSERT_ARG(NULL != buf);

    /* Decrement the reference count */
    if (1 == atomic_fetch_sub(&buf->refcount, 1)) {
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

    TSL_BUG_ON(false == can_process);

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

        for (size_t i = 0; i < dthr->nr_fm_samples; i++) {
            TSL_BUG_ON(LPF_PCM_OUTPUT_LEN <= dthr->nr_pcm_samples);
            /* Get the complex conjugate of the prior sample - calculate the instantaneous phase difference between the two. */
            int32_t b_re =  dthr->last_fm_re,
                    b_im = -dthr->last_fm_im,
                    a_re =  dthr->fm_samp_out_buf[2 * i    ],
                    a_im =  dthr->fm_samp_out_buf[2 * i + 1];

            int32_t s_re = a_re * b_re - a_im * b_im,
                    s_im = a_im * b_re + a_re * b_im;

            /* TODO: This needs to be made full-integer. Calculate the instantaneous phase difference. */
            double sample = atan2((double)(s_im >> Q_15_SHIFT), (double)(s_re >> Q_15_SHIFT));
            dthr->pcm_out_buf[dthr->nr_pcm_samples] = (int16_t)(sample/M_PI * (double)(1ul << Q_15_SHIFT));

            dthr->nr_pcm_samples++;

            /* Store the last sample processed */
            dthr->last_fm_re = a_re;
            dthr->last_fm_im = a_im;
        }

        /* x. Write out the resulting PCM samples */
        if (0 > write(dthr->fifo_fd, dthr->pcm_out_buf, dthr->nr_pcm_samples * sizeof(int16_t))) {
            int errnum = errno;
            PANIC("Failed to write %zu bytes to the output fifo. Reason: %s (%d)", sizeof(int16_t) * dthr->nr_pcm_samples,
                    strerror(errnum), errnum);
        }

        TSL_BUG_IF_FAILED(direct_fir_can_process(&dthr->fir, &can_process, NULL));

        /* We're done with this batch of samples, woohoo */
        dthr->nr_fm_samples = 0;
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
 * \param lpf_taps The taps for the direct-form FIR. These are real, the filter must be at baseband.
 * \param lpf_nr_taps The number of taps in the direct-form FIR. This is the order of the filter + 1.
 * \param offset_hz The offset, in hertz, from the center frequency
 * \param sample_rate The sample rate of the input stream
 * \param decimation The decimation factor for the output from this FIR.
 *
 * \return A_OK on success, an error code otherwise
 */
static
aresult_t _demod_fir_prepare(struct demod_thread *thr, double *lpf_taps, size_t lpf_nr_taps, int32_t offset_hz, uint32_t sample_rate, int decimation)
{
    aresult_t ret = A_OK;

    int16_t *coeffs = NULL;
    double f_offs = -2.0 * M_PI * (double)offset_hz / (double)sample_rate;
#ifdef _DUMP_LPF
    int64_t power = 0;
    double dpower = 0.0;
#endif /* defined(_DUMP_LPF) */
    size_t base = lpf_nr_taps;

    DIAG("Preparing LPF for offset %d Hz", offset_hz);

    TSL_ASSERT_ARG(NULL != thr);
    TSL_ASSERT_ARG(NULL != lpf_taps);
    TSL_ASSERT_ARG(0 != lpf_nr_taps);

    if (FAILED(ret = TACALLOC((void *)&coeffs, lpf_nr_taps, sizeof(int16_t) * 2, SYS_CACHE_LINE_LENGTH))) {
        MFM_MSG(SEV_FATAL, "NO-MEM", "Out of memory for FIR.");
        goto done;
    }

#ifdef _DUMP_LPF
    fprintf(stderr, "lpf_shifted_%d = [\n", offset_hz);
#endif /* defined(_DUMP_LPF) */

    for (size_t i = 0; i < lpf_nr_taps; i++) {
        /* Calculate the new tap coefficient */
        const double complex lpf_tap = cexp(CMPLX(0, f_offs * (double)i)) * lpf_taps[i];
        const double q15 = 1ll << Q_15_SHIFT;
#ifdef _DUMP_LPF
        double ptemp = 0;
        int64_t samp_power = 0;
#endif

        /* Calculate the Q31 coefficient */
        coeffs[       i] = (int16_t)(creal(lpf_tap) * q15);
        coeffs[base + i] = (int16_t)(cimag(lpf_tap) * q15);

#ifdef _DUMP_LPF
        ptemp = sqrt( (creal(lpf_tap) * creal(lpf_tap)) + (cimag(lpf_tap) * cimag(lpf_tap)) );
        samp_power = sqrt( ((int64_t)coeffs[i] * (int64_t)coeffs[i]) + ((int64_t)coeffs[base + i] * (int64_t)coeffs[base + i]) );

        power += samp_power;
        dpower += ptemp;

        fprintf(stderr, "    complex(%f, %f), %% (%d, %d)\n", creal(lpf_tap), cimag(lpf_tap), coeffs[i], coeffs[base + i]);
#endif /* defined(_DUMP_LPF) */
    }
#ifdef _DUMP_LPF
    fprintf(stderr, "];\n");
    fprintf(stderr, "%% Total power: %llu (%016llx) (%f)\n", power, power, dpower);
#endif /* defined(_DUMP_LPF) */

    /* Create a Direct Type FIR implementation */
    TSL_BUG_IF_FAILED(direct_fir_init(&thr->fir, lpf_nr_taps, coeffs, &coeffs[base], decimation, thr, true, sample_rate, offset_hz));

done:
    if (NULL != coeffs) {
        TFREE(coeffs);
    }

    return ret;
}

aresult_t demod_thread_new(struct demod_thread **pthr, unsigned core_id, struct frame_alloc *samp_buf_alloc,
        int32_t offset_hz, uint32_t samp_hz, const char *out_fifo, int decimation_factor,
        double *lpf_taps, size_t lpf_nr_taps)
{
    aresult_t ret = A_OK;

    struct demod_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != samp_buf_alloc);
    TSL_ASSERT_ARG(NULL != out_fifo && '\0' != *out_fifo);
    TSL_ASSERT_ARG(0 != decimation_factor);
    TSL_ASSERT_ARG(NULL != lpf_taps);
    TSL_ASSERT_ARG(0 != lpf_nr_taps);

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
    if (FAILED(ret = _demod_fir_prepare(thr, lpf_taps, lpf_nr_taps, offset_hz, samp_hz, decimation_factor))) {
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