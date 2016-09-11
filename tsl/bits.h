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

#include <tsl/version.h>
#include <tsl/cal.h>

#include <stdint.h>
#include <stddef.h>

/**
 * \brief Generate a bit field mask
 * Generate a mask for a bit field that is x bits long
 * \param x The number of bits in the mask
 */
#define TSL_MASK(x) ((1ull << (x)) - 1)

/**
 * \brief Generate a shifted value that can be ORed into a bit field
 * Deposit the provided value at a specified offset of the specified length;
 * this can then be ORed into a register or similar
 * \param val The value
 * \param off The offset in the bit field
 * \param len The length of the value, in bits
 */
#define TSL_DEPOSIT(val, off, len) (((val) & TSL_MASK((len))) << (off))

/**
 * \brief Extract a range of bits
 * Extract a range of bits from the provided word.
 * \param val The input to extract the field from
 * \param off The offset of the field
 * \param len the length of the field
 */
#define TSL_EXTRACT(val, off, len) (((val) >> (off)) & TSL_MASK((len)))

/**
 * \brief Count the leading zeros of a uint32_t
 */
static inline
unsigned tsl_clz32(uint32_t value)
{
    uint32_t ret = value;

    __asm__ __volatile__  (
        "\tlzcnt %[val], %[val]\n"
        : [val]"+r"(ret)
        :
        : "cc"
    );

    return ret;
}

/**
 * \brief Count the leading zeros of a uint64_t
 */
static inline
unsigned tsl_clz64(uint64_t value)
{
    uint64_t ret = value;

    __asm__ __volatile__  (
        "\tlzcntq %[val], %[val]\n"
        : [val]"+r"(ret)
        :
        : "cc"
    );

    return (unsigned)ret;
}

/**
 * \brief Round a 64-bit integer up to the nearest power of two
 */
static inline
uint64_t tsl_round_up_2_64(uint64_t value)
{
    uint64_t v = value;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

/**
 * \brief Round a 32-bit integer up to the nearest power of two
 */
static inline
uint32_t tsl_round_up_2_32(uint32_t value)
{
    uint32_t v = value;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v;
}

/**
 * \brief Scan from MSB to LSB for a set bit and return the position of the bit.
 */
static inline CAL_AGGRESSIVE_INLINE
size_t tsl_bit_scan_rev_64(uint64_t val)
{
    return sizeof(val) * __CHAR_BIT__ - 1 - (size_t)__builtin_clzl(val);
}

/**
 * \brief Count the population of bits in the given uint64_t
 */
static inline CAL_AGGRESSIVE_INLINE
size_t tsl_pop_count_64(uint64_t val)
{
    return (size_t)__builtin_popcountll(val);
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
