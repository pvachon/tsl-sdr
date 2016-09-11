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

#include <tsl/version.h>
#include <tsl/errors.h>
#include <tsl/cal.h>

#include <assert.h>

#ifdef _TSL_DEBUG
#include <stdio.h>
#endif /* _TSL_DEBUG */

#ifndef _TSL_DEBUG
/** \brief Assert state about an argument
 * Macro to check if a given assertion about an argument is true
 * \param x Assertion about an argument
 * \note Function must return an aresult_t
 */
#define TSL_ASSERT_ARG(x) \
    do {                                \
        if (CAL_UNLIKELY(!(x))) {       \
            return A_E_BADARGS;         \
        }                               \
    } while (0)

/**
 * \brief Assert state about an argument only in debug builds
 * Macro to check if a given assertion is true, but only applicable
 * in debug builds. When building a performance build, the TSL_ASSERT_DEBUG
 * macro becomes a no-op
 */
#define TSL_ASSERT_ARG_DEBUG(...)

#else /* defined(_TSL_DEBUG) */

/** \brief Assert state about an argument
 * Macro to check if a given assertion about an argument is true
 * \param x Assertion about an argument
 * \note Function must return an aresult_t
 */
#define TSL_ASSERT_ARG(x) \
    do {                                \
        if (CAL_UNLIKELY(!(x))) {       \
            printf("Assertion failed! %s:%d (function %s): " #x " == FALSE\n", \
                    __FILE__, __LINE__, __FUNCTION__); \
            return A_E_BADARGS;         \
        }                               \
    } while (0)

/**
 * \brief Assert state about an argument only in debug builds
 * Macro to check if a given assertion is true, but only applicable
 * in debug builds. When building a performance build, the TSL_ASSERT_DEBUG
 * macro becomes a no-op
 */
#define TSL_ASSERT_ARG_DEBUG(x) TSL_ASSERT_ARG((x))

#endif /* defined(_TSL_DEBUG) */

/**
 * \brief Assert that a string is non-empty
 *
 * Checks that a string pointer is non-NULL and that the string contains a value
 */
#define TSL_ASSERT_STRING(_str) \
    do {                                    \
        TSL_ASSERT_ARG(NULL != (_str));     \
        TSL_ASSERT_ARG('\0' != *(_str));    \
    } while (0)

/**
 * \brief Check that a pointer passed by reference is not-NULL.
 *
 * Checks that a pointer, i.e. a reference parameter, is set, and then checks that
 * the pointer's location doesn't contain a NULL value either.
 */
#define TSL_ASSERT_PTR_BY_REF(_p) \
    do {                                \
        TSL_ASSERT_ARG(NULL != (_p));   \
        TSL_ASSERT_ARG(NULL != *(_p));  \
    } while (0)

#include <tsl/panic.h>

/**
 * Panic with a useful message if an event occurs that can't be backed out. This
 * should really only be used if the world is in the process of melting down
 * beacause of data structure corruption or hardware failure.
 */
#define TSL_BUG_ON(x) \
    do {                                    \
        if (CAL_UNLIKELY((x))) {            \
            PANIC("BUG: " #x " == TRUE");   \
        }                                   \
    } while (0)

/**
 * Helper macro -- generate a BUGcheck if the function called has generated a failing
 * result
 */
#define TSL_BUG_IF_FAILED(x) TSL_BUG_ON(((x)))

/**
 * Internal function used to issue the WARN message. Use TSL_WARN_ON instead of calling
 * this directly.
 *
 * \param line_no The line number for the warning.
 * \param filename The name of the source file the warning was issued from
 * \param msg A message (typically just the assertion).
 *
 * \see TSL_WARN_ON
 */
void __tsl_do_warn(int line_no, const char *filename, const char *msg, ...);

/**
 * Emit a warning with a partial backtrace in the event that the assertion is true.
 */
#define TSL_WARN_ON(x) \
    ({                                                          \
        int __warn = (x);                                       \
        if (CAL_UNLIKELY(__warn)) {                             \
            __tsl_do_warn(__LINE__, __FILE__, "WARN: " #x " == true");\
        }                                                       \
        __warn;                                                 \
    })

#define __TSL_STATIC_ASSERT_EMIT(_c, _line)                     \
        static int __x_static_assertion_## _line[1 - 2*(!(_c))] CAL_UNUSED;

#define __TSL_STATIC_ASSERT_AGG(_c, _line) __TSL_STATIC_ASSERT_EMIT(_c, _line)

/**
 * Compile-time static assertion.
 */
#define TSL_STATIC_ASSERT(_c) __TSL_STATIC_ASSERT_AGG(_c, __LINE__)
