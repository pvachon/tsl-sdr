#pragma once
/*
  Copyright (c) 2014, 12Sided Technology, LLC
  Author: Phil Vachon <pvachon@12sidedtech.com>
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

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

#include <tsl/cal.h>
#include <tsl/result.h>

/* Forward declarations */
struct thread;
struct cpu_mask;

/** \file worker_thread.h
 * This file contains the declarations and state definitions for how non-offload worker
 * threads are to be managed. Worker threads typically are core to an application's
 * functionality and thus should not be managed as a work pool's cooperative multitasking
 * is.
 */

#define WORKER_THREAD_CPU_MASK_ANY              0xfffffffful

/**
 * Enumeration of possible states for the work thread active state machine.
 */
enum worker_thread_state {
    /**
     * Initial state of the work thread. Can transition from this state
     * to any other state.
     */
    WORKER_THREAD_STATE_IDLE = 0,

    /**
     * After the thread has been kicked off by a thread start request, the
     * work thread transitions to this state.
     *
     * Valid transitions:
     *  * WORKER_THREAD_STATE_RUNNING
     *    Thread has done its startup work and is ready to accept requests
     *  * WORKER_THREAD_STATE_SHUTDOWN_REQUESTED
     *    Thread has been told to shut down by its controlling thread
     *  * WORKER_THREAD_STATE_SHUTDOWN
     *    Thread has been shut down and is ready to be join'd to the controlling thread
     */
    WORKER_THREAD_STATE_STARTING_UP,
    /**
     * Once the startup routine for the work thread has finished, this state
     * indicates the thread is ready and doing its job.
     * Valid transitions:
     *  * WORKER_THREAD_STATE_SHUTDOWN_REQUESTED
     *    Thread has been told to shut down by its controlling thread
     *  * WORKER_THREAD_STATE_SHUTDOWN
     *    Thread has been shut down and is ready to be join'd to the controlling thread
     */
    WORKER_THREAD_STATE_RUNNING,

    /**
     * Controlling thread for this thread has ordered the work thread to shut down.
     *
     * Valid transitions:
     *  * WORKER_THREAD_STATE_SHUTDOWN
     *    Thread has been shut down and is ready to be join'd to the controlling thread
     */
    WORKER_THREAD_STATE_SHUTDOWN_REQUESTED,

    /**
     * Terminal state for a thread. The thread is shut down and has been flagged as being
     * ready to join the parent thread.
     */
    WORKER_THREAD_STATE_SHUTDOWN,
};

/**
 * A thread work function -- a function that is called when a thread is to be started.
 */
typedef aresult_t (*worker_thread_work_func_t)(void *params);

/**
 * Structure representing worker thread state. Should be stored cache aligned in host
 * structure. This structure is allocated by the caller, so can be typically embedded
 * in another structure readily.
 */
struct worker_thread {
    /**
     * The current state of the work thread
     * \see enum worker_thread_state
     */
    int st CAL_CACHE_ALIGNED;

    /**
     * The raw thread handle state, used in managing system-level threading details.
     * \see struct thread
     */
    struct thread *thr;

    /**
     * The CPU mask for this thread.
     */
    struct cpu_mask *mask;

    /**
     * The thread work function that is called when the thread has been kicked off.
     */
    worker_thread_work_func_t work_func;
};


/**
 * Create a new worker thread given the provided parameters.
 *
 * \param thr The memory to populate with information about the worker thread.
 * \param work_func The worker thread action function. Takes over execution of the thread (i.e. is a loop)
 * \param cpu_core The CPU core to bind this work thread to. If this is set to WORKER_THREAD_CPU_MASK_ANY,
 *                 the then the thread is not bound to a particular CPU core.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t worker_thread_new(struct worker_thread *thr, worker_thread_work_func_t work_func, unsigned int cpu_core);

/**
 * Create a new worker thread, binding the thread to the cores specified in the given CPU mask.
 *
 * \param thr The memory to populate with information about the worker thread.
 * \pram work_func The worker function to be called once the thread has started up.
 * \param pmsk The CPU core mask to bind this thread to, passed by reference. If this function returns successfully,
 *             the object is owned by the thread object, pmsk set to NULL. Otherwise, it is the caller's responsibility
 *             to clean up.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t worker_thread_new_mask(struct worker_thread *thr, worker_thread_work_func_t work_func, struct cpu_mask **pmsk);

/**
 * Request that a worker thread shut itself down.
 *
 * \param thr The thread to request a shutdown of
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t worker_thread_request_shutdown(struct worker_thread *thr);

/**
 * Attempt to reap the worker thread, after shutdown has completed.
 *
 * \param thr The worker thread to reap.
 * \return A_OK on success, an error code otherwise.
 */
aresult_t worker_thread_delete(struct worker_thread *thr);

#include <ck_pr.h>

/**
 * End of loop check to determine if the work thread is allowed to continue running
 * or if it should shut down and get ready to join the parent thread.
 *
 * \param thr The thread to check on the runstate
 * \return 1 if the thread is running, 0 if it has been shut down or told to shut down.
 *
 * \note This assertion can be used anywhere the thread needs to be checked on, however,
 *       there is no effort to assert the correctness of `thr`.
 */
static inline
int worker_thread_is_running(struct worker_thread *thr)
{
    return (ck_pr_load_int(&thr->st) == WORKER_THREAD_STATE_RUNNING);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
