#pragma once

#if !defined(__DO_INCLUDE_TSL_TIME_PRIV_H__)
#error Do not include tsl/time_priv.h directly -- include tsl/time.h instead.
#endif /* !defined(__DO_INCLUDE_TSL_TIME_PRIV_H__) */

#include <tsl/cal.h>
#include <stdint.h>

extern
uint64_t _tsl_proc_clock_offset;

#ifdef __x86_64__

extern uint64_t _tsl_clock_coeff;

static CAL_AGGRESSIVE_INLINE
uint64_t __tsl_get_cpu_timer(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi) :: "%rbx","%rcx","memory");
    return ( (uint64_t)lo) | ( ((uint64_t)hi)<<32 );
}

/*
 * Get the time, in nanoseconds, since the start of the process. Optimized to a single
 * multiplication, on x86-64
 */
static CAL_AGGRESSIVE_INLINE
uint64_t tsl_get_clock_monotonic(void)
{
    uint64_t ret = 0;
    uint64_t timer = __tsl_get_cpu_timer() - _tsl_proc_clock_offset;

    __asm__ __volatile__ (
        "\tmulq %[src]\n"
        : "=d"(ret)
        : "a"(timer), [src] "rm"(_tsl_clock_coeff)
        : "memory","cc");

    return ret;
}
#else

extern
uint64_t _tsl_clock_freq;

/*
 * Unoptimized version of the TSL get clock monotonic, naive version
 */
uint64_t tsl_get_clock_monotonic(void)
{
    return time_get_time();
}
#endif

aresult_t tsl_time_init(void);

