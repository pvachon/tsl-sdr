#pragma once
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

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

#include <tsl/result.h>
#include <tsl/cal.h>

#include <stdint.h>

/**
 * Get the current system time
 */
uint64_t time_get_time(void);

/**
 * Get a monotonic time. This time is relative to an arbitrary epoch that is consistent
 * across a single process, but is not consistent between processes. As such, these
 * timestamps cannot be shared between processes.
 */
static CAL_AGGRESSIVE_INLINE
uint64_t tsl_get_clock_monotonic(void);

/**
 * Get the current system time as a ns/sec fraction
 */
aresult_t time_get_time_frac(uint32_t *ts_sec, uint32_t *ts_nsec_frac);

#define __DO_INCLUDE_TSL_TIME_PRIV_H__
#include <tsl/time_priv.h>
#undef __DO_INCLUDE_TSL_TIME_PRIV_H__

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
