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
#include <tsl/list.h>
#include <tsl/errors.h>
#include <tsl/alloc.h>
#include <tsl/time.h>
#include <tsl/diag.h>
#include <tsl/rbtree.h>

#include <stdint.h>

/* Forward declarations */
struct timer;

/**
 * The timer manager object, which is local to a context that executes within a thread. Typically,
 * you'll have one timer manager per thread, but it's also not unreasonable to create a timer
 * manager within a cooperatively multitasked context. Basically, anywhere a timer manager will run
 * "effectively" atomically.
 *
 * The timer manager maintains an rb-tree of timers, sorted by firing time (with a slight hack to
 * linearize timers that will execute simultaneously, incrementing the time by 1 if a conflict is
 * found).
 */
struct timer_manager {
    /**
     * Timestamp of the next event to fire for quick checks
     */
    uint64_t next_fire;

    /**
     * Min-heap of timers, sorted by time
     */
    struct rb_tree timer_pq;
};

/**
 * Initialize a new timer manager. A timer manager should always be executed from the same context,
 * i.e. the same cooperatively multitasked process or the same pinned thread.
 *
 * \param mgr Manager to initialize
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t timer_manager_init(struct timer_manager *mgr);

/**
 * Clean up a timer manager instance that will no longer be used.
 *
 * \param mgr The timer to clean up.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t timer_manager_cleanup(struct timer_manager *mgr);

/**
 * Fire the first pending timer that is ready to fire, if applicable. Return time
 * until the next timer might be ready to fire, in nanoseconds, by reference.
 *
 * \param mgr The timer manager to inspect and manipulate
 *
 * \return A_OK on success, an error code otherwise.
 */
static inline
aresult_t timer_manager_fire_next(struct timer_manager *mgr);

/**
 * Function pointer for a timer firing event
 */
typedef aresult_t (*timer_fire_func_t)(struct timer *);

#define TIMER_STATE_DISARMED ((uint64_t)0)

/**
 * A structure describing a single timer. Do not directly manipulate anything in this structure, other
 * than the fire function.
 */
struct timer {
    /**
     * Function to be fired when the timer's deadline interval has passed
     */
    timer_fire_func_t fire;

    /**
     * Earliest time at which the timer is to fire (nanoseconds since epoch)
     */
    uint64_t next_firing;

    /**
     * The red-black tree node used for the min-heap of timers
     */
    struct rb_tree_node tpq_node;
};

/**
 * Arm the given timer, using the given timer_manager to track the timer's
 * lifecycle. Timer fires at now + usecs.
 *
 * \param tmr The timer to arm
 * \param mgr The manager to associate the timer with
 * \param usecs The time from now to fire the timer at
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t timer_arm(struct timer *tmr, struct timer_manager *mgr, uint64_t usecs);

/**
 * Arm the given timer, using the given timer_manager to track the timer's
 * lifecycle, specifically firing the timer at `when` nanoseconds from the epoch.
 *
 * \param tmr The timer to arm
 * \param mgr The manager to associate the timer with
 * \param when The number of nanoseconds since the epoch to fire the timer at.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t timer_arm_at(struct timer *tmr, struct timer_manager *mgr, uint64_t when);

/**
 * Disarm the specified timer, preventing it from firing.
 *
 * \param tmr The timer to disarm.
 * \param mgr the timer manager this timer is armed in.
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t timer_disarm(struct timer *tmr, struct timer_manager *mgr);

/* Inlined function implementation */

static inline
aresult_t timer_manager_fire_next(struct timer_manager *mgr)
{
    aresult_t ret = A_OK;
    struct timer *tmr = NULL;
    struct rb_tree_node *nd = NULL;
    uint64_t ts = tsl_get_clock_monotonic();

    TSL_ASSERT_ARG_DEBUG(NULL != mgr);

    /*
     * Check if we've passed the next firing interval -- avoid touching anything
     * outside of the timer manager if we can.
     */
    if (0 == mgr->next_fire || ts < mgr->next_fire) {
        goto done;
    }

    /*
     * Pull the next timer to be fired off the PQ.
     */
    TSL_BUG_IF_FAILED(rb_tree_get_rightmost(&mgr->timer_pq, &nd));

    if (NULL != nd) {
        tmr = BL_CONTAINER_OF(nd, struct timer, tpq_node);

        /* Simple sanity checks - make sure state is clean surrounding the timer */
        TSL_BUG_ON(ts < tmr->next_firing);
        TSL_BUG_ON(NULL == tmr->fire);

        /* Remove the timer from the min-heap */
        TSL_BUG_IF_FAILED(rb_tree_remove(&mgr->timer_pq, nd));

        /* Fire the timer */
        tmr->fire(tmr);

        tmr->next_firing = 0;

        /*
         * Since the next timer item is likely cache-hot, update the next firing time in
         * the timer manager.
         */
        TSL_BUG_IF_FAILED(rb_tree_get_rightmost(&mgr->timer_pq, &nd));
        if (NULL != nd) {
            tmr = BL_CONTAINER_OF(nd, struct timer, tpq_node);
            mgr->next_fire = tmr->next_firing;
        }
    }

done:
    return ret;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
