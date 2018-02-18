#pragma once

#include <tsl/result.h>

struct receiver;
struct config;

aresult_t file_worker_thread_new(struct receiver **pthr, struct config *cfg);
