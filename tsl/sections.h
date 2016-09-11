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

#include <tsl/cal.h>

/**
 * \brief Mark an object as being in a special loadable section
 * Given a prefix for a section and the name of a struct, puts a pointer to
 * that struct in the specified section.
 * \param prefix The prefix for this section
 * \param name The name of the object to export
 */
#define CR_LOADABLE(prefix, name) \
    CAL_SECTION(#prefix) __typeof__(name)* name##_ptr = &(name)

/**
 * \brief Iterate through all loadables
 * Iterate through all items in the section "prefix", in the style
 * of a for-loop.
 * \param reference The variable to pick up the object reference
 * \param prefix The section name prefix used
 */
#define CR_FOR_EACH_LOADABLE(__reference, prefix) \
    extern __typeof__(__reference) __start_##prefix[];            \
    extern __typeof__(__reference) __stop_##prefix[];             \
    (__reference) = *__start_##prefix;                            \
    for (__typeof__(__reference)* __cursor = __start_##prefix;    \
         __cursor != __stop_##prefix;                             \
         ++__cursor, (__reference) = *__cursor)

/**
 * \brief Return the count of all loadables
 * Return the count of the number of loadable items in the section
 * "prefix". Typically used for diagnostic purposes.
 * \param prefix The section name prefix
 */
#define CR_GET_LOADABLE_SIZE(prefix, type) \
    ({                                          \
        extern type* __start_##prefix[];        \
        extern type* __stop_##prefix[];         \
        size_t count = (__stop_##prefix - __start_##prefix);  \
        count;                                  \
    })
