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
#if HAVE_RTLSDR
#include <multifm/rtl_sdr_if.h>
#include <rtl-sdr.h>
#endif /* HAVE_RTLSDR */

#if HAVE_DESPAIRSPY
#include <multifm/airspy_if.h>
#endif

#include <multifm/receiver.h>

#include <filter/sample_buf.h>

#include <config/engine.h>

#include <app/app.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/frame_alloc.h>

#include <stdlib.h>
#include <complex.h>
#include <string.h>

#if HAVE_RTLSDR
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
#endif

static
void _usage(const char *name)
{
    fprintf(stderr, "usage: %s [Config File 1]{, Config File 2, ...} | %s -h\n", name, name);
#if HAVE_RTLSDR
    _do_dump_rtl_sdr_devices();
#endif
}

int main(int argc, const char *argv[])
{
    int ret = EXIT_FAILURE;
    struct config *cfg CAL_CLEANUP(config_delete) = NULL;
    struct config device = CONFIG_INIT_EMPTY;
    struct receiver *rx_thr = NULL;
    const char *dev_type = NULL;

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

    /* Figure out what kind of device we should initialize */
    if (FAILED(config_get(cfg, &device, "device"))) {
        MFM_MSG(SEV_FATAL, "MALFORMED-CONFIG", "Configuration is missing 'device' stanza. Aborting.");
        goto done;
    }

    if (FAILED(config_get_string(&device, &dev_type, "type"))) {
        MFM_MSG(SEV_FATAL, "MALFORMED-CONFIG", "The 'device' stanza is missing a 'type' specification. Aborting.");
        goto done;
    }

    /* Prepare the RTL-SDR thread and demod threads */
    if (!strncmp(dev_type, "rtlsdr", 6)) {
#if HAVE_RTLSDR
        TSL_BUG_IF_FAILED(rtl_sdr_worker_thread_new(&rx_thr, cfg));
#else
        MFM_MSG(SEV_FATAL, "RTLSDR-NOT-SUPPORTED", "RTL-SDR devices are not supported by this build.");
        goto done;
#endif
    } else if (!strncmp(dev_type, "airspy", 6)) {
#if HAVE_DESPAIRSPY
        TSL_BUG_IF_FAILED(airspy_worker_thread_new(&rx_thr, cfg));
#else
        MFM_MSG(SEV_FATAL, "AIRSPY-NOT-SUPPORTED", "Airspy devices are not supported by this build.");
        goto done;
#endif
    } else {
        MFM_MSG(SEV_FATAL, "UNKNOWN-DEV-TYPE", "Unknown device type: '%s'", dev_type);
        goto done;
    }

    TSL_BUG_IF_FAILED(receiver_set_mute(rx_thr, false));

    MFM_MSG(SEV_INFO, "CAPTURING", "Starting capture and demodulation process.");
    TSL_BUG_IF_FAILED(receiver_start(rx_thr));

    while (app_running()) {
        sleep(1);
    }

    DIAG("Terminating.");

    ret = EXIT_SUCCESS;
done:
    receiver_cleanup(&rx_thr);
    return ret;
}

