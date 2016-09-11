#pragma once

#include <tsl/assert.h>
#include <stdbool.h>

#ifndef __INCLUDED_TSL_COROUTINE_H__
#error Need to include coro/detail.h through coroutine.h
#endif

typedef void (_coro_internal_init_func_t)(struct coro_ctx *ctx, void *payload);

#ifdef __x86_64__
#define __INCLUDED_TSL_CORO_DETAIL_H__
#include <tsl/coro/x86_64.h>
#undef __INCLUDED_TSL_CORO_DETAIL_H__
#else
#error Register Context management for coroutines needs to be implemented for your architecture
#endif /* __x86_64__ */

#define CORO_CONTINUE           (1)

#define CORO_TERM_REQD          1

/**
 * Current execution state for the given coroutine greenthread
 */
enum coro_run_state {
    /**
     * Greenthread is uninitialized and has not been run yet
     */
    CORO_STATE_UNINITIALIZED = 0,

    /**
     * Coroutine has been initialized but is not running yet
     */
    CORO_STATE_INITIALIZED,

    /**
     * Coroutine is starting
     */
    CORO_STATE_STARTING,

    /**
     * Greenthread is sleeping
     */
    CORO_STATE_SLEEPING,

    /**
     * Greenthread is active (i.e. scheduled and running)
     */
    CORO_STATE_ACTIVE,

    /**
     * Greenthread has been terminated.
     */
    CORO_STATE_TERMINATED,
};

/**
 * Context for an instance of a coroutine execution context.
 */
struct coro_ctx {
    /**
     * Register execution context for this coroutine
     */
    struct coro_reg_state st;

    /**
     * Mailbox for triggering a coroutine state information
     */
    uint64_t mailbox;

    /**
     * Main entry point for this coroutine.
     */
    coro_main_func_t main;

    /**
     * The parent context
     */
    struct coro_thread_state *parent;

    /**
     * Size of the stack area mapping.
     */
    size_t stack_bytes;

    /**
     * Base of the stack mapping
     */
    void *stack_mapping;

    /**
     * Final result of the coroutine
     */
    aresult_t final;

    /**
     * The current state of the coroutine (useful for debugging)
     */
    enum coro_run_state run_state;
};

struct coro_thread_state {
    /**
     * Thread context. Not initialized unless this thread has been swapped from.
     */
    struct coro_reg_state thread_ctx;

    /**
     * Currently live coroutine, helpful for debugging.
     */
    struct coro_ctx *live_ctx;
};

static inline
bool coro_ctx_term_req(struct coro_ctx *ctx)
{
    TSL_BUG_ON(NULL == ctx);
    return !!(ctx->mailbox & CORO_TERM_REQD);
}
