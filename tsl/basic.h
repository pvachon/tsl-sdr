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

#include <stddef.h>
#include <stdint.h>
/** \brief Round to the nearest 2^n, where n is given.
 * \param val Value to round
 * \param n Power of two to round to
 */
#define BL_ROUND_POW2(val, n) (( ((val) + (n) - 1) >> (n) ) << (n))

/**
 * Returns non-zero if number is a power of two
 * \param x value to check
 */
#define BL_IS_POW2(x) ((x != 0) && ((x & (~x + 1)) == x))

/**
 * Finds the next power of two for a value
 */
static inline
uint64_t next_power_of_two(uint64_t v) {

    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;

    return ++v;
}

/**
 * Finds the log2 of an integer value
 */
static inline
uint64_t log2_u64(const uint64_t x) {
  uint64_t y;
  __asm__ ( "\tbsr %1, %0\n"
      : "=r"(y)
      : "r" (x)
      : "cc"
  );
  return y;
}

/** \brief Get containing structure pointer
 * Extract a pointer to the structure that contains the given pointer.
 * Usually used when an embedded structure is used to reference the entire
 * enclosing structure.
 * \param pointer The pointer to the embedded structure.
 * \param type The type of the containing structure
 * \param member The name of the member of type that is the embedded structure
 * \return pointer to containing structure
 */
#define BL_CONTAINER_OF(pointer, type, member) \
    ({ __typeof__( ((type *)0)->member ) *__memb = (pointer); \
       (type *)( (char *)__memb - offsetof(type, member) ); })

/** \brief Get the number of elements in an array
 * Given a statically-defined array, return the size of the array, in count of
 * entities.
 * \param x The array in question
 * \return Number of elements in the array.
 */
#define BL_ARRAY_ENTRIES(x) (sizeof(x)/sizeof(__typeof__(*x)))

/** \brief Find the minimum of a pair of arguments
 *
 */
#define BL_MAX2(x, y) ( (x) < (y) ? (y) : (x) )

/** \brief Find the minimum of a pair of arguments
 *
 */
#define BL_MIN2(x, y) ( (x) < (y) ? (x) : (y) )

/** \brief Find the minimum of a specified set of 3 arguments.
 */
#define BL_MIN3(x, y, z) ( ((x) < (y)) ? BL_MIN2((x), (z)) : BL_MIN2((y), (z)) )
