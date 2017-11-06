#pragma once

#include <tsl/result.h>

struct receiver;
struct config;


/**
 * Create a new UHD receiver thread
 */
aresult_t uhd_worker_thread_new(struct receiver **pthr, struct config *cfg);

