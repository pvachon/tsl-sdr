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

/** \file speed.h
 * Really low-level primitives for building up code that uses AVX/SSE for
 * vector operations
 */

#ifdef __AVX__
#define TSL_SSE_PREPARE() \
    ({ __asm__ __volatile__ ( "    vzeroupper\n" ); })
#else
#define TSL_SSE_PREPARE()
#endif

#define TSL_LOAD_ALIGNED_128(__src, __reg)              \
        ({                                              \
            __asm__ __volatile__ (                      \
                    "    movdqa %[src], %%" #__reg "\n"  \
                    :                                   \
                    : [src]"m"(*(__src))                \
                    : #__reg                            \
                );                                      \
        })

#define TSL_LOAD_ALIGNED_256(__src, __reg, __xmm)       \
        ({                                              \
            __asm__ __volatile__ (                      \
                    "    vmovdqa %[src], %%" #__reg "\n" \
                    :                                   \
                    : [src]"m"(*(__src))                \
                    : #__xmm                            \
                );                                      \
        })

#define TSL_STORE_ALIGNED_128(__src, __reg)             \
        ({                                              \
            __asm__ __volatile__ (                      \
                    "    movntdq %%" #__reg ", %[src]\n"   \
                    :                                   \
                    : [src]"m"(*(__src))                \
                    : #__reg                            \
                );                                      \
        })

#define TSL_STORE_ALIGNED_256(__src, __reg, __xmm)      \
        ({                                              \
            __asm__ __volatile__ (                      \
                    "    vmovdqa %%" #__reg ", %[src]\n" \
                    :                                   \
                    : [src]"m"i(*(__src))               \
                    : #__xmm                            \
                );                                      \
        })

/**
 * Prefetch a cache line containing _addr into L1. Should only use this if you're absolutely
 * sure that you're going to use the data pointed to at L1. Otherwise, you risk polluting L1
 * and reducing effective throughput of the L1 cache.
 *
 * Logical operation: PREFETCH(_addr)
 */
#define TSL_PREFETCH_ALL_LEVELS(_addr) \
    ({                                                  \
        __asm__ __volatile__ (                          \
            "    prefetcht0 %[addr]\n"                  \
            : : [addr]"m"(*(_addr))                     \
            : "memory"                                  \
        );                                              \
     })

/**
 * Prefetch cache line containing _addr. This is typically with respect to anticipating
 * a read access. This will typically fetch to L2 (and higher) levels of cache, avoiding
 * polluting L1. This is helpful if a load might be dependent and ultimately unused, avoiding
 * wasting L1 cache bandwidth.
 *
 * Logical operation: PREFETCH(_addr)
 */
#define TSL_PREFETCH_L2_HIGHER(_addr) \
    ({                                                  \
        __asm__ __volatile__ (                          \
            "    prefetcht1 %[addr]\n"                  \
            : :[addr]"m"(*(_addr))                      \
            : "memory"                                  \
       );                                               \
     })

/**
 * Prefetch data in anticipation of a write. CPU hint.
 *
 * Logical operation: FETCH_WITH_EXCLUSIVE_OWNERSHIP(_addr)
 */
#define TSL_PREFETCH_WRITE(_addr) \
    ({                                                  \
        __asm__ __volatile__ (                          \
            "    prefetchw %[addr]\n"                   \
            : :[addr]"m"(*(_addr))                      \
            : "memory"                                  \
        );                                              \
     })
