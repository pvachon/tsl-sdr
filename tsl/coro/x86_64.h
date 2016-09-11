#pragma once

#ifndef __INCLUDED_TSL_CORO_DETAIL_H__
#error This file must be included via the coroutine detail header
#endif

#ifndef __ASM__
#include <tsl/cal.h>
#include <stdint.h>
/**
 * 8 byte registers
 */
typedef uint64_t coro_reg_t;

/**
 * Status registers are 32-bits at most
 */
typedef uint32_t coro_sr_t;

/**
 * The coro register state contains all callee saved registers, per the x86_64 System V ABI.
 */
struct coro_reg_state {
    coro_reg_t rbx;
    coro_reg_t rsp;
    coro_reg_t rbp;
    coro_reg_t r12;
    coro_reg_t r13;
    coro_reg_t r14;
    coro_reg_t r15;
    coro_sr_t x87_cw;
    coro_sr_t mxcsr;

    /*
     * The 5 argument slots. These are only used when calling a coroutine for the first time.
     */
    coro_reg_t rsi;
    coro_reg_t rdi;
    coro_reg_t rdx;
    coro_reg_t rcx;
    coro_reg_t r8;
    coro_reg_t r9;
} CAL_PACKED;
#else

#define CRS_OFFS_RBX                0
#define CRS_OFFS_RSP                8
#define CRS_OFFS_RBP                16
#define CRS_OFFS_R12                24
#define CRS_OFFS_R13                32
#define CRS_OFFS_R14                40
#define CRS_OFFS_R15                48
#define CRS_OFFS_X85_SW             56

#define CRS_OFFS_RSI                64
#define CRS_OFFS_RDI                72
#define CRS_OFFS_RDX                80
#define CRS_OFFS_RCX                88
#define CRS_OFFS_R8                 96
#define CRS_OFFS_R9                 104

#endif

aresult_t coro_plat_init(struct coro_ctx *ctx, _coro_internal_init_func_t main, void *priv, void *stack_ptr, size_t stack_bytes);
void coro_plat_ctx_swap(struct coro_reg_state *tgt, struct coro_reg_state *cur);
void coro_plat_ctx_swap_start(struct coro_reg_state *tgt, struct coro_reg_state *cur);
