/*
 *  multifm.c - A channelizer for extracting multiple narrowband channels
 *      from a wideband captured chunk of the spectrum.
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

#include <multifm/multifm.h>
#include <multifm/rtl_sdr_if.h>

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <app/app.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/frame_alloc.h>

#include <stdlib.h>
#include <complex.h>

#include <rtl-sdr.h>

#define RTL_SDR_DEFAULT_NR_SAMPLES      (16 * 32 * 512/2)

static
void _do_dump_rtl_sdr_devices(void)
{
    uint32_t nr_devs = rtlsdr_get_device_count();

    if (0 == nr_devs) {
        MFM_MSG(SEV_WARNING, "NO-DEVS-FOUND", "No RTL-SDR devices found.");
        goto done;
    }

    MFM_MSG(SEV_INFO, "DEVS-FOUND", "Found %u RTL-SDR devices", nr_devs + 1);

    for (uint32_t i = 0; i < nr_devs; i++) {
        const char *name = rtlsdr_get_device_name(i);
        fprintf(stderr, "%2u: %s\n", i, name);
    }

done:
    return;
}

static
void _usage(const char *name)
{
    fprintf(stderr, "usage: %s [Config File 1]{, Config File 2, ...} | %s -h\n", name, name);
    _do_dump_rtl_sdr_devices();
}

int main(int argc, const char *argv[])
{
    int ret = EXIT_FAILURE;
    struct config *cfg CAL_CLEANUP(config_delete) = NULL;
    struct rtl_sdr_thread *rtl_thr = NULL;
    uint32_t center_freq_hz = 0,
             sample_freq_hz = 0;
    int sr_hz = -1,
        center_hz = -1,
        nr_samp_bufs = -1;
    struct frame_alloc *falloc CAL_CLEANUP(frame_alloc_delete) = NULL;

    if (argc < 2) {
        _usage(argv[0]);
        goto done;
    }

    /* Parse and load the configurations from the command line */
    TSL_BUG_IF_FAILED(config_new(&cfg));

    for (int i = 1; i < argc; i++) {
        if (FAILED(config_add(cfg, argv[i]))) {
            MFM_MSG(SEV_FATAL, "MALFORMED-CONFIG", "Configuration file [%s] is malformed.", argv[i]);
            goto done;
        }
        DIAG("Added configuration file '%s'", argv[i]);
    }

    /* Initialize the app framework */
    TSL_BUG_IF_FAILED(app_init("multifm", cfg));
    TSL_BUG_IF_FAILED(app_sigint_catch(NULL));

    /* Generate the demodulation states from configs (FIXME useful error messages) */
    if (FAILED(config_get_integer(cfg, &sr_hz, "sampleRateHz"))) {
        MFM_MSG(SEV_INFO, "NO-SAMPLE-RATE", "Need to specify a sample rate, in Hertz.");
        goto done;
    }

    TSL_BUG_IF_FAILED(config_get_integer(cfg, &center_hz, "centerFreqHz"));

    if (FAILED(config_get_integer(cfg, &nr_samp_bufs, "nrSampBufs"))) {
        MFM_MSG(SEV_INFO, "DEFAULT-SAMP-BUFS", "Setting sample buffer count to 64");
        nr_samp_bufs = 64;
    }

    center_freq_hz = center_hz;
    sample_freq_hz = sr_hz;

    MFM_MSG(SEV_INFO, "SAMPLE-RATE", "Sample rate is set to %u Hz", sample_freq_hz);
    MFM_MSG(SEV_INFO, "CENTER-FREQ", "Center Frequency is %u Hz", center_freq_hz);

    /*
     * Create the memory frame allocator for sample buffers
     */
    TSL_BUG_IF_FAILED(frame_alloc_new(&falloc,
                sizeof(struct sample_buf) +
                    RTL_SDR_DEFAULT_NR_SAMPLES * sizeof(int16_t) * 2,
                nr_samp_bufs));

    /* Prepare the RTL-SDR thread and demod threads */
    TSL_BUG_IF_FAILED(rtl_sdr_worker_thread_new(cfg, center_freq_hz, sample_freq_hz, falloc, &rtl_thr));

    rtl_thr->muted = false;

    MFM_MSG(SEV_INFO, "CAPTURING", "Starting capture and demodulation process.");

    while (app_running()) {
        sleep(1);
    }

    DIAG("Terminating.");

    ret = EXIT_SUCCESS;
done:
    rtl_sdr_worker_thread_delete(&rtl_thr);
    return ret;
}

