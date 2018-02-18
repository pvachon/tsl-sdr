/*
 *  demod.c - Filtering and demodulation thread for FM channelization
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

#include <multifm/demod.h>
#include <multifm/multifm.h>

#include <filter/direct_fir.h>
#include <filter/sample_buf.h>
#include <filter/polyphase_fir.h>
#include <filter/complex.h>

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
#include <string.h>

#ifdef _USE_ARM_NEON
#include <arm_neon.h>
#endif

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

        dthr->total_nr_fm_samples += nr_samples;

        if (-1 != dthr->debug_signal_fd) {
            if (0 > write(dthr->debug_signal_fd, dthr->fm_samp_out_buf + dthr->nr_fm_samples, nr_samples * 2 * sizeof(int16_t))) {
                int errnum = errno;
                MFM_MSG(SEV_WARNING, "CANT-WRITE-DEBUG-FILE", "Unable to write %zu bytes to post-demod debug file. Reason: %s (%d). Skipping.",
                        nr_samples * 2 * sizeof(int16_t), strerror(errnum), errnum);
            }
        }

        dthr->nr_fm_samples += nr_samples;

        /* 2. Perform quadrature demod, write to output demodulation buffer. */
        /* TODO: smarten this up a lot - this sucks */
        dthr->nr_pcm_samples = 0;

        const float to_q15 = (float)(1 << Q_15_SHIFT);

        for (size_t i = 0; i < dthr->nr_fm_samples; i++) {
            TSL_BUG_ON(LPF_PCM_OUTPUT_LEN <= dthr->nr_pcm_samples);
            /* Get the complex conjugate of the prior sample, negating the phase term */
            int32_t b_re =  dthr->last_fm_re,
                    b_im = -dthr->last_fm_im,
                    a_re =  dthr->fm_samp_out_buf[2 * i    ],
                    a_im =  dthr->fm_samp_out_buf[2 * i + 1];
            int32_t s_re = 0,
                    s_im = 0;

            /* Calculate the phase difference */
            s_re = a_re * b_re - a_im * b_im;
            s_im = a_re * b_im + a_im * b_re;

            /* Convert from cartesian coordinates to a phase angle */
            /* TODO: This needs to be made full-integer */
            float phi = fast_atan2f((float)s_im, (float)s_re);

            /* Scale by pi (since atan2 returns an angle in (-pi,pi]), convert back to Q.15 */
            float phi_scaled = (phi/M_PI) * to_q15;
            dthr->pcm_out_buf[dthr->nr_pcm_samples] = (int16_t)phi_scaled;

            dthr->nr_pcm_samples++;

            /* Store the last sample processed */
            dthr->last_fm_re = a_re;
            dthr->last_fm_im = a_im;
        }

        if (true == dthr->block_dc) {
            TSL_BUG_IF_FAILED(dc_blocker_apply(&dthr->dc_blk, dthr->pcm_out_buf, dthr->nr_pcm_samples));
        }

        /* Apply the polyphase resampler, if we're asked */
        if (NULL != dthr->pfir) {
            /* Allocate a bounce buffer for the output */

            /* Resample using the polyphase resampler */
        }

        /* x. Write out the resulting PCM samples */
        if (0 > write(dthr->fifo_fd, dthr->pcm_out_buf, dthr->nr_pcm_samples * sizeof(int16_t))) {
            int errnum = errno;
            if (errnum == EPIPE) {
                if (0 == dthr->nr_dropped_samples) {
                    MFM_MSG(SEV_WARNING, "FIFO-REMOTE-END-DISCONNECTED", "Remote end of FIFO disconnected. "
                            "Until a process picks up the FIFO, we're dropping samples.");
                }
                dthr->nr_dropped_samples += dthr->nr_pcm_samples;
            } else {
                PANIC("Failed to write %zu bytes to the output fifo. Reason: %s (%d)",
                        sizeof(int16_t) * dthr->nr_pcm_samples,
                        strerror(errnum), errnum);
            }
        } else if (0 != dthr->nr_dropped_samples) {
            MFM_MSG(SEV_WARNING, "FIFO-RESUMED", "Remote FIFO end reconnected. Dropped %zu samples in the interim.",
                    dthr->nr_dropped_samples);
            dthr->nr_dropped_samples = 0;
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

    TSL_BUG_IF_FAILED(direct_fir_cleanup(&thr->fir));
    if (NULL != thr->pfir) {
        TSL_BUG_IF_FAILED(polyphase_fir_delete(&thr->pfir));
    }

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
aresult_t _demod_fir_prepare(struct demod_thread *thr, const double *lpf_taps, size_t lpf_nr_taps, int32_t offset_hz, uint32_t sample_rate, int decimation, double gain)
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
        const double complex lpf_tap = gain * cexp(CMPLX(0, f_offs * (double)i)) * lpf_taps[i];
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
    TSL_BUG_IF_FAILED(direct_fir_init(&thr->fir, lpf_nr_taps, coeffs, &coeffs[base], decimation, true, sample_rate, offset_hz));

done:
    if (NULL != coeffs) {
        TFREE(coeffs);
    }

    return ret;
}

aresult_t demod_thread_new(struct demod_thread **pthr, unsigned core_id,
        int32_t offset_hz, uint32_t samp_hz, const char *out_fifo, int decimation_factor,
        const double *lpf_taps, size_t lpf_nr_taps,
        unsigned resample_decimate, unsigned resample_interpolate, const int16_t *resample_filter_taps,
        size_t nr_resample_filter_taps,
        const char *fir_debug_output,
        double dc_block_pole, bool enable_dc_block,
        double channel_gain)
{
    aresult_t ret = A_OK;

    struct demod_thread *thr = NULL;

    TSL_ASSERT_ARG(NULL != pthr);
    TSL_ASSERT_ARG(NULL != out_fifo && '\0' != *out_fifo);
    TSL_ASSERT_ARG(0 != decimation_factor);
    TSL_ASSERT_ARG(NULL != lpf_taps);
    TSL_ASSERT_ARG(0 != lpf_nr_taps);
    TSL_ASSERT_ARG(1.0 > dc_block_pole && 0.0 <= dc_block_pole);

    if (0 != resample_decimate && 0 != resample_interpolate) {
        TSL_ASSERT_ARG(NULL != resample_filter_taps);
        TSL_ASSERT_ARG(0 != nr_resample_filter_taps);
    }

    *pthr = NULL;

    if (FAILED(ret = TZAALLOC(thr, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    thr->fifo_fd = -1;
    thr->debug_signal_fd = -1;

    /* Initialize the work queue */
    if (FAILED(ret = work_queue_new(&thr->wq, 128))) {
        goto done;
    }

    /* Initialize the mutex */
    if (0 != pthread_mutex_init(&thr->wq_mtx, NULL)) {
        ret = A_E_INVAL;
        goto done;
    }

    /* Initialize the condition variable */
    if (0 != pthread_cond_init(&thr->wq_cv, NULL)) {
        ret = A_E_INVAL;
        goto done;
    }

    /* Initialize the filter */
    if (FAILED(ret = _demod_fir_prepare(thr, lpf_taps, lpf_nr_taps, offset_hz, samp_hz, decimation_factor, channel_gain))) {
        goto done;
    }

    /* Set up the DC blocker, if applicable */
    thr->block_dc = enable_dc_block;

    if (true == enable_dc_block) {
        TSL_BUG_IF_FAILED(dc_blocker_init(&thr->dc_blk, dc_block_pole));
    }

    /* TODO If applicable, initialize the polyphase rational resampler */
    if (0 != nr_resample_filter_taps) {
        TSL_BUG_ON(NULL == resample_filter_taps);
        TSL_BUG_IF_FAILED(polyphase_fir_new(&thr->pfir, nr_resample_filter_taps, resample_filter_taps,
                    resample_interpolate, resample_decimate));
    }

    /* Open the debug output file, if applicable */
    if (NULL != fir_debug_output && '\0' != *fir_debug_output) {
        if (0 > (thr->debug_signal_fd = open(fir_debug_output, O_WRONLY))) {
            ret = A_E_INVAL;
            MFM_MSG(SEV_FATAL, "CANT-OPEN-SIGNAL-DEBUG", "Unable to open signal debug dump file '%s'", fir_debug_output);
            goto done;
        }
    }

    /* Open the output FIFO */
    if (0 > (thr->fifo_fd = open(out_fifo, O_WRONLY))) {
        ret = A_E_INVAL;
        MFM_MSG(SEV_FATAL, "CANT-OPEN-FIFO", "Unable to open output fifo '%s'", out_fifo);
        goto done;
    }

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

            if (NULL != thr->pfir) {
                TSL_BUG_IF_FAILED(polyphase_fir_delete(&thr->pfir));
            }

            TSL_BUG_IF_FAILED(direct_fir_cleanup(&thr->fir));

            TFREE(thr);
        }
    }
    return ret;
}

