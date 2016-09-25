/*
  Copyright (c) 2014, 12Sided Technology LLC.
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
#include <tsl/timer.h>
#include <tsl/errors.h>
#include <tsl/time.h>
#include <tsl/alloc.h>
#include <tsl/diag.h>
#include <tsl/rbtree.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

static
int __timer_manager_sort_timers(void *lhs, void *rhs)
{
    struct timer *tlhs = (struct timer *)lhs;
    struct timer *trhs = (struct timer *)rhs;

    return (int64_t)trhs->next_firing - (int64_t)tlhs->next_firing;
}

aresult_t timer_manager_init(struct timer_manager *mgr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != mgr);

    if (FAILED(rb_tree_new(&mgr->timer_pq, __timer_manager_sort_timers))) {
        DIAG("Failed to allocate a heap for the live timers.");
        ret = A_E_NOMEM;
        goto done;
    }

done:
    return ret;
}

aresult_t timer_manager_cleanup(struct timer_manager *mgr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != mgr);

    /* TODO: we should iterate through the rb-tree and call a cleanup event, maybe? */
    memset(mgr, 0, sizeof(*mgr));

    return ret;
}

static
aresult_t __timer_manager_add_timer(struct timer *tmr, struct timer_manager *mgr)
{
    aresult_t ret = A_OK;

    bool inserted = false;

    TSL_ASSERT_ARG_DEBUG(NULL != tmr);
    TSL_ASSERT_ARG_DEBUG(NULL != mgr);

    /*
     * Ugly hack - since you can have timers that will conflict, and the rb-tree requires
     * unique entries, we try to fit the same timer within the same microsecond as what has
     * been requested by the user as the rb-tree key (but the firing remains the same, since
     * we're just treating the rb-tree as a min-heap).
     */
    for (int i = 0; i < 1000; i++) {
        if (FAILED_UNLIKELY(rb_tree_insert(&mgr->timer_pq, (void *)(tmr->next_firing + i), &tmr->tpq_node))) {
            /* There was a conflict, so repeat the insertion attempt with a slightly tweaked timestamp */
            continue;
        }

        inserted = true;

        /* If applicable, adjust the next firing timer */
        if (0 == mgr->next_fire || mgr->next_fire > tmr->next_firing) {
            mgr->next_fire = tmr->next_firing;
        }
    }

    if (CAL_UNLIKELY(false == inserted)) {
        ret = A_E_BUSY;
        goto done;
    }

done:
    return ret;
}

aresult_t timer_arm(struct timer *tmr, struct timer_manager *mgr, uint64_t usecs)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != tmr);
    TSL_ASSERT_ARG_DEBUG(NULL != mgr);
    /* Must be some time in the future */
    TSL_ASSERT_ARG_DEBUG(0 < usecs);

    tmr->next_firing = tsl_get_clock_monotonic() + (usecs * 1000);

    ret = __timer_manager_add_timer(tmr, mgr);

    return ret;
}

aresult_t timer_arm_at(struct timer *tmr, struct timer_manager *mgr, uint64_t when)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != tmr);
    TSL_ASSERT_ARG_DEBUG(NULL != mgr);

    tmr->next_firing = when;

    ret = __timer_manager_add_timer(tmr, mgr);

    return ret;
}

aresult_t timer_disarm(struct timer *tmr, struct timer_manager *mgr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != tmr);
    TSL_ASSERT_ARG_DEBUG(NULL != mgr);

    if (0 != tmr->next_firing) {
        TSL_BUG_IF_FAILED(rb_tree_remove(&mgr->timer_pq, &tmr->tpq_node));
        tmr->next_firing = 0;
    }

    return ret;
}
