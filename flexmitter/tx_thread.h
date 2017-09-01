#pragma once

#include <tsl/result.h>

struct tx_thread;
struct config;

aresult_t tx_thread_new(struct tx_thread **pthr, struct config *thr_cfg);
aresult_t tx_thread_delete(struct tx_thread **pthr);
aresult_t tx_thread_start(struct tx_thread *thr);

