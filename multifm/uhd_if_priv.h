#pragma once

#include <multifm/receiver.h>

#include <tsl/diag.h>

#include <uhd.h>

#define UHD_MSG(sev, sys, msg, ...)     MESSAGE("UHD", sev, sys, msg, ##__VA_ARGS__)

struct uhd_worker_thread {
    struct receiver rx;
    uhd_usrp_handle dev_hdl;
    uhd_rx_streamer_handle rx_stream;
    size_t *channels;
    size_t nr_channels;
};

