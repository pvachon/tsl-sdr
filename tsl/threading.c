/*
  Copyright (c) 2013, Phil Vachon <phil@cowpig.ca>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * TSL abstraction for threading -- pthreads edition
 */
#include <tsl/assert.h>
#include <tsl/cpumask.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/safe_alloc.h>
#include <tsl/threading.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <pthread.h>

#include <ck_pr.h>

enum thread_state {
    THREAD_STATE_IDLE,          /** Thread is idle, not running */
    THREAD_STATE_RUNNING,       /** Thread is running, status quo */
    THREAD_STATE_TERMINATED     /** Thread has been shut down, not yet joined */
};

struct thread {
    pthread_t hdl;
    enum thread_state state;
    thread_function_t func;
    struct cpu_mask *mask;
    void *params;
};

static
struct thread *__helper_thread_alloc(void)
{
    struct thread *thr = NULL;
    if (FAILED(TZALLOC(thr))) {
      goto done;
    }

    thr->state = THREAD_STATE_IDLE;
    thr->func = NULL;

done:
    return thr;
}

aresult_t thread_create(struct thread **thread,
                        thread_function_t func,
                        struct cpu_mask *mask)
{
    aresult_t res = A_OK;
    struct thread *thr = NULL;

    TSL_ASSERT_ARG(thread != NULL);
    TSL_ASSERT_ARG(func != NULL);

    *thread = NULL;

    thr = __helper_thread_alloc();

    if (thr == NULL) {
        res = A_E_NOMEM;
        goto done;
    }

    thr->mask = mask;
    thr->func = func;

    *thread = thr;

done:
    return res;
}

aresult_t thread_running(struct thread *thread, int *val)
{
    TSL_ASSERT_ARG(thread);
    TSL_ASSERT_ARG(val);

    *val = !!(thread->state == THREAD_STATE_RUNNING);

    return A_OK;
}

/**
 * Wrapper thunk for managing the lifecycle of a thread
 * \param thread_info The thread information structure
 * \return The error code from thread->func
 */
static
void *__thread_wrapper_func(void *thread_info)
{
    struct thread *thread = (struct thread *)thread_info;
    aresult_t ret = 0;

    if (NULL != thread->mask) {
        ret = cpu_mask_apply(thread->mask);

        if (ret != A_OK) {
            DIAG("FAILURE: could not apply CPU affinity mask.");
            goto done;
        }
    }

    thread->state = THREAD_STATE_RUNNING;
    ret = thread->func(thread->params);

done:
    thread->state = THREAD_STATE_TERMINATED;

    pthread_exit((void *)(ssize_t)ret);
}

aresult_t thread_start(struct thread *thread,
                       void *params)
{
    aresult_t res = A_OK;

    TSL_ASSERT_ARG(thread);
    TSL_ASSERT_ARG(params);

    thread->params = params;

    if (pthread_create(&thread->hdl, NULL, __thread_wrapper_func, thread) != 0) {
        DIAG("Failed to start thread!");
        res = A_E_NOTHREAD;
        goto done;
    }

done:
    return res;
}

aresult_t thread_join(struct thread *thread,
                      aresult_t *result)
{
    aresult_t ret = A_OK;
    int state = 0;

    TSL_ASSERT_ARG(thread != NULL);

    state = ck_pr_load_int((int *)&thread->state);

    if (state == THREAD_STATE_TERMINATED) {
        DIAG("Warning: about to pthread_join a thread that hasn't terminated, hope you had other state checks...");
    }

    void *res = NULL;

    if (pthread_join(thread->hdl, &res) != 0) {
        ret = A_E_NOTHREAD;
        goto done;
    }

    *result = (aresult_t)(intptr_t)res;

    thread->state = THREAD_STATE_IDLE;
    thread->params = NULL;

done:
    return ret;
}

aresult_t thread_destroy(struct thread **thread)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(thread != NULL);
    TSL_ASSERT_ARG(*thread != NULL);

    struct thread *thr = *thread;

    if (thr->state != THREAD_STATE_IDLE) {
        ret = A_E_BUSY;
        goto done;
    }

    memset(thr, 0, sizeof(struct thread));

    TFREE(*thread);

done:
    return ret;
}
