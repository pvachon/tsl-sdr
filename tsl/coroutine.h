#pragma once

#include <tsl/result.h>
#include <tsl/cal.h>
#include <stdbool.h>

/*
 * Thread state for a coroutine host
 */
struct coro_thread_state;

/*
 * Context used for managing coroutine lifecycle
 */
struct coro_ctx;

/**
 * Initial entry point for a coroutine
 */
typedef aresult_t (*coro_main_func_t)(struct coro_ctx *ctx, void *state);

/**
 * Get the current per-thread coroutine thread state.
 */
aresult_t coro_thread_get(struct coro_thread_state **pst);

/**
 * Determine if the current thread context is the outermost context.
 *
 * \note This function should only be used for error handling
 */
aresult_t coro_is_outer_context(bool *is_outer);

/**
 * Get the currently executing coroutine context
 *
 * \note This really only should be used for error handling, please.
 */
aresult_t coro_get_inner_context(struct coro_ctx **pctx);

/**
 * Initialize a new coroutine context. Creates stack region, and defines
 * the main entry point.
 *
 * \param init The coro_ctx to be initialized
 * \param stack_bytes The number of bytes of stack space to allocate
 * \param main The main entry point for this coroutine
 * \param priv A private state pointer to be passed to the coroutine function's main.
 */
aresult_t coro_ctx_init(struct coro_ctx *init, size_t stack_bytes, coro_main_func_t main, void *priv);

/**
 * Call a coroutine for the first time, to initialize it
 *
 * \param st The calling thread's context
 * \param ctx The coroutine's context
 */
aresult_t coro_start(struct coro_thread_state *st, struct coro_ctx *ctx);

/**
 * Switch context to the given coroutine (i.e. wake it up). This is called from the outside (i.e. caller)
 * context.
 */
aresult_t coro_ctx_swap(struct coro_thread_state *st, struct coro_ctx *ctx);

/**
 * Yield the given coroutine's context. This is called from inside the coroutine to yield the CPU back to
 * the caller, without terminating the context.
 */
aresult_t coro_yield(struct coro_ctx *ctx);

/**
 * Terminate the coroutine, signalling that it wishes to die and never be awoken again. RIP.
 */
void CAL_NORETURN coro_terminate(struct coro_ctx *ctx, aresult_t code);

/**
 * Tag this coroutine for termination (clean up resources and exit please). Called from the outside (i.e. caller)
 * context.
 */
aresult_t coro_ctx_sig_term(struct coro_ctx *ctx);

/**
 * Check if this coroutine has been scheduled for termination
 */
static inline
bool coro_ctx_term_req(struct coro_ctx *ctx);

/**
 * Release the given coro context. While the memory is still mostly valid, calling at this point would be invalid,
 * and result in crashes and tears.
 *
 * Note: this TSL_WARN_ON's if the current final value for the coroutine is CORO_CONTINUE
 */
aresult_t coro_ctx_release(struct coro_ctx *ctx);

#define __INCLUDED_TSL_COROUTINE_H__
#include <tsl/coro/detail.h>
#undef __INCLUDED_TSL_COROUTINE_H__
