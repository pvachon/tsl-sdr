/*
 *  x86_64_coro_init.c - Coroutine details specific for x86_64
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

aresult_t coro_plat_init(struct coro_ctx *ctx, _coro_internal_init_func_t main, void *priv, void *stack_ptr, size_t stack_bytes)
{
    aresult_t ret = A_OK;

    void *sp = NULL;

    TSL_ASSERT_ARG(NULL != ctx);
    TSL_ASSERT_ARG(NULL != main);
    TSL_ASSERT_ARG(NULL != priv);
    TSL_ASSERT_ARG(NULL != stack_ptr);
    TSL_ASSERT_ARG(0 != stack_bytes);

    if ((stack_bytes & 0x7) != 0) {
        ret = A_E_INVAL;
        DIAG("Stack region length must be an even 8 byte region");
        goto done;
    }

    if (((ptrdiff_t)stack_ptr & 0x7) != 0) {
        ret = A_E_INVAL;
        DIAG("Stack address must be based at an even 8 byte address");
        goto done;
    }

    /* Set the entry point for the coroutine */
    sp = stack_ptr + stack_bytes - 16;
    *(ptrdiff_t *)(sp) = (ptrdiff_t)main;
    *(ptrdiff_t *)(sp + 8) = 0xaa55aa55aa55aa55ull;

    /* Set the context for the coroutine */
    ctx->st.rdi = (coro_reg_t)ctx;
    ctx->st.rsi = (coro_reg_t)priv;

    /* Set up the stack pointer */
    ctx->st.rsp = (coro_reg_t)sp;

done:
    return ret;
}
