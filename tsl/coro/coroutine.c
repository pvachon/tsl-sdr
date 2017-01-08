/*
 *  coroutine.c - Lightweight Coroutine Library
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <tsl/coroutine.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>

CAL_THREAD_LOCAL struct coro_thread_state __coro_this_thread = { .live_ctx = NULL };

aresult_t coro_thread_get(struct coro_thread_state **pst)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != pst);

    *pst = &__coro_this_thread;

    return ret;
}

aresult_t coro_is_outer_context(bool *is_outer)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != is_outer);

    *is_outer = __coro_this_thread.live_ctx != NULL;

    return ret;
}

aresult_t coro_get_inner_context(struct coro_ctx **pctx)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != pctx);

    *pctx = __coro_this_thread.live_ctx;

    return ret;
}

static CAL_NORETURN
void _coro_terminate(struct coro_ctx *ctx, aresult_t result_code)
{
    ctx->final = result_code;
    ctx->run_state = CORO_STATE_TERMINATED;
    /* Swap the state of the current coroutine out, so the master thread can continue execution */
    coro_plat_ctx_swap(&ctx->parent->thread_ctx, &ctx->st);
    PANIC("Coroutine woken after terminal sleep.");
}

/**
 * Entrypoint for all coroutines. Checks a few state details,
 */
static
void _coro_entrypt(struct coro_ctx *ctx, void *payload)
{
    aresult_t ret = A_OK;

    TSL_BUG_ON(NULL == ctx);
    TSL_BUG_ON(NULL == ctx->parent);
    TSL_BUG_ON(NULL == ctx->main);

    ctx->run_state = CORO_STATE_ACTIVE;

    ret = ctx->main(ctx, payload);

    _coro_terminate(ctx, ret);
}

aresult_t coro_ctx_init(struct coro_ctx *init, size_t stack_bytes, coro_main_func_t main, void *priv)
{
    aresult_t ret = A_OK;

    void *stack_ptr = NULL;

    TSL_ASSERT_ARG(NULL != init);
    TSL_ASSERT_ARG(NULL != main);

    /*
     * Allocate a stack region
     */
    if (MAP_FAILED == (stack_ptr = mmap(NULL, stack_bytes, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_GROWSDOWN, -1, 0))) {
        DIAG("Failed to map stack region, aborting.");
        ret = A_E_NOMEM;
        goto done;
    }

    init->main = main;
    init->stack_bytes = stack_bytes;
    init->stack_mapping = stack_ptr;
    init->final = CORO_CONTINUE;

    TSL_BUG_IF_FAILED(coro_plat_init(init, _coro_entrypt, priv, stack_ptr, stack_bytes));

    init->run_state = CORO_STATE_INITIALIZED;

done:
    return ret;
}

aresult_t coro_start(struct coro_thread_state *st, struct coro_ctx *ctx)
{
    TSL_ASSERT_ARG(NULL != st);
    TSL_ASSERT_ARG(NULL != ctx);

    TSL_BUG_ON(CORO_STATE_INITIALIZED != ctx->run_state);

    ctx->parent = st;
    ctx->final = CORO_CONTINUE;
    ctx->run_state = CORO_STATE_STARTING;
    st->live_ctx = ctx;

    /* Call the stub that swaps the context with argument passing */
    coro_plat_ctx_swap_start(&ctx->st, &st->thread_ctx);

    return ctx->final;
}

aresult_t coro_ctx_swap(struct coro_thread_state *st, struct coro_ctx *ctx)
{
    TSL_ASSERT_ARG(NULL != st);
    TSL_ASSERT_ARG(NULL != ctx);

    ctx->parent = st;
    st->live_ctx = ctx;

    /* Call the stub that swaps the two contexts */
    coro_plat_ctx_swap(&ctx->st, &st->thread_ctx);

    st->live_ctx = NULL;

    return ctx->final;
}

aresult_t coro_yield(struct coro_ctx *ctx)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != ctx);
    TSL_BUG_ON(NULL == ctx->parent);
    TSL_BUG_ON(CORO_STATE_ACTIVE != ctx->run_state);

    ctx->final = CORO_CONTINUE;
    ctx->run_state = CORO_STATE_SLEEPING;

    /* Call the stub that swaps the two contexts */
    coro_plat_ctx_swap(&ctx->parent->thread_ctx, &ctx->st);

    ctx->run_state = CORO_STATE_ACTIVE;

    return ret;
}

void CAL_NORETURN coro_terminate(struct coro_ctx *ctx, aresult_t code)
{
    TSL_BUG_ON(ctx->run_state == CORO_STATE_ACTIVE);
    _coro_terminate(ctx, code);
}

aresult_t coro_ctx_sig_term(struct coro_ctx *ctx)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != ctx);

    TSL_BUG_ON(ctx->run_state != CORO_STATE_SLEEPING);

    ctx->mailbox |= CORO_TERM_REQD;

    return ret;
}

aresult_t coro_ctx_release(struct coro_ctx *ctx)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != ctx);

    TSL_WARN_ON(CORO_STATE_TERMINATED != ctx->run_state);

    if (NULL != ctx->stack_mapping) {
        TSL_BUG_ON(0 == ctx->stack_bytes);
        munmap(ctx->stack_mapping, ctx->stack_bytes);
    }

    memset(ctx, 0, sizeof(*ctx));

    return ret;
}
