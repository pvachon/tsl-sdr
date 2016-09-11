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

/* Declaration of the Compiler Abstraction Library primitives */

#include <tsl/version.h>

/**
 * \file cal.h
 * \brief The The Standard Library Compiler Abstraction Library
 *
 * The Compiler Abstraction Library (CAL) provides a generic abstraction
 * intended to allow users to take advantage of common compiler primitives
 * that can be used to control how code is emitted, generated or
 * gathered, and not risk the portability of the code to other compilers
 * or environments.
 **/

/* Declarations for GCC 4.7 and later */

/* ----------------------------------------------------- */
/* Optimization-related Primitives                       */
/* ----------------------------------------------------- */

/**
 * \brief Tag branch as likely to be taken
 * \param x Boolean conditional
 */
#define CAL_LIKELY(x) \
    __builtin_expect(!!(x), 1)

/**
 * \brief Tag branch as unlikely to be taken
 * \param x Boolean conditional
 */
#define CAL_UNLIKELY(x) \
    __builtin_expect(!!(x), 0)

/**
 * \brief Tag a function as never returning
 */
#define CAL_NORETURN \
    __attribute__((noreturn))


/* ----------------------------------------------------- */
/* Code Inlining Control Primitives                      */
/* ----------------------------------------------------- */

/**
 * \brief Tag a function to never be inlined
 */
#define CAL_NOINLINE \
    __attribute__((noinline))

/**
 * \brief Tag a function to always be inlined
 */
#define CAL_AGGRESSIVE_INLINE \
    inline __attribute__((always_inline))

/* ----------------------------------------------------- */
/* Data Alignment Primitives                             */
/* ----------------------------------------------------- */

/**
 * \brief Specify the alignment of a data member
 * \param num_bytes Number of bytes to align to
 */
#define CAL_ALIGN(num_bytes) \
    __attribute__((aligned( (num_bytes) )))

/**
 * \brief Specify the alignment of a data member or structure
 * to a cache line.
 */
#define CAL_CACHE_ALIGNED CAL_ALIGN(SYS_CACHE_LINE_LENGTH)

/**
 * \brief Specify that a structure is to be unpadded
 */
#define CAL_PACKED \
    __attribute__((packed))

/**
 * \brief Specify that a structure is both packed and padded to
 *        an alignment of bytes
 */
#define CAL_PACKED_ALIGNED(num_bytes) \
    __attribute__((packed, aligned(num_bytes)))


/* ----------------------------------------------------- */
/* Compiler Warnings                                     */
/* ----------------------------------------------------- */

/**
 * \brief Tag that a particular structure or function is deprecated
 */
#define CAL_DEPRECATED \
    __attribute__((deprecated))

/**
 * \brief Tag that a particular function cannot have NULL args
 *
 */
#define CAL_NONNULL(...) \
    __attribute__((nonnull(##__VA_ARGS__)))

/**
 * \brief Emit an error
 * If the tagged function or value is not eliminated by dead-code analysis,
 * emit an error message for diagnostics
 * \param message_string The message string to emit
 */
#define CAL_ERROR(message_string) \
    __attribute__((error(message_string)))

/**
 * \brief Emit an warning
 * If the tagged function or value is not eliminated by dead-code analysis,
 * emit an warning message for diagnostics
 * \param message_string The message string to emit
 */
#define CAL_WARNING(message_string) \
    __attribute__((warning(message_string)))

/**
 * \brief Tag a particular function or variable as being legally unused
 */
#define CAL_UNUSED \
    __attribute__((unused))

/**
 * \brief Tag a symbol as being used and should always be compiled
 */
#define CAL_USED \
    __attribute__((used))

/**
 * \brief Warn if result is unused
 * Generates a compiler warning if the value returned from the function is ignored
 */
#define CAL_ALWAYS_USE_RESULT \
    __attribute__((warn_unused_result))

#define CAL_CHECKED CAL_ALWAYS_USE_RESULT

/* ----------------------------------------------------- */
/* Linking-related Goodies                               */
/* ----------------------------------------------------- */

/**
 * \brief Specify the section the tagged object goes in
 * \param section_name the name of the section
 */
#define CAL_SECTION(section_name) \
    __attribute__((section(section_name)))

/**
 * \brief Specify that the given function is to be hidden
 * Hide a symbol so it isn't visible in the exports table of object code.
 */
#define CAL_HIDDEN \
    __attribute__((visibility("hidden")))

/**
 * \brief Specify that the given function is to be visible
 * Ensure that a symbol is visible in the exports table of object code.
 */
#define CAL_EXPORT \
    __attribute__((visibility("default")))

/**
 * \brief Specify a symbol is weak
 * Specify that the given symbol is to be considered weak, and the declaration can
 * be overridden by a stronger symbol during the linking phase.
 */
#define CAL_WEAK \
    __attribute__((weak))

/**
 * Specify an implicit destructor call for when a variable falls out of scope.
 */
#define CAL_CLEANUP(func) \
    __attribute__((cleanup(func)))

/**
 * Attribute for a variable specifying that the given variable should reside in
 * thread-local storage.
 */
#define CAL_THREAD_LOCAL __thread

/**
 * Specify that a function is pure (i.e. no side effects, a call with the same args will
 * result in the same outputs)
 */
#define CAL_PURE __attribute__((pure))

/**
 * A constant expression comparison that forces compile-time evaluation of a conditional.
 *
 * \param cond The const expression condition to evaluate
 * \param if_true If the condition is true
 * \param if_false If the condition is false
 *
 * \return Sets the return value to whatever the if_true expression if cond is true at
 *         compile time, if_false otherwise.
 */
#define CAL_EVAL_CONSTEXPR(cond, if_true, if_false) ({ __builtin_choose_expr((cond), if_true, if_false); })
