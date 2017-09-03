#pragma once

#include <tsl/result.h>

struct tx_thread;
struct config;
struct work_queue;
struct frame_alloc;

aresult_t tx_thread_new(struct tx_thread **pthr, struct config *thr_cfg, struct work_queue *wq, struct frame_alloc *fa);
aresult_t tx_thread_delete(struct tx_thread **pthr);
aresult_t tx_thread_start(struct tx_thread *thr);

