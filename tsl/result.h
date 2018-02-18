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
#include <tsl/bits.h>
#include <tsl/cal.h>

#include <stdint.h>

/* Declaration of system-wide generic error codes and types
 */

/** \typedef aresult_t
 * \brief Return type for a result code
 * A structured return code used to return result states from an operation.
 * Structure:
 * bit 31: error flag
 * bit 30: warning flag
 * bits 16-29: facility code (14 bits)
 * bits 0-16: return code (16 bits)
 */
typedef int32_t aresult_t;

#define ARESULT_ERROR_BIT           31
#define ARESULT_WARNING_BIT         30
#define ARESULT_FACILITY_OFFSET     16
#define ARESULT_FACILITY_SIZE       14
#define ARESULT_CODE_OFFSET         0
#define ARESULT_CODE_SIZE           16

/** \brief Determine if an error condition has been signalled
 * Return 1 if the provided code indicates an error, 0 otherwise.
 */
#define FAILED(x) (CAL_UNLIKELY((!!((x) & (1 << ARESULT_ERROR_BIT)))))

/** \brief Determine if an unlikely error condition was signalled
 * Return 1 if the provided code indicates an error, 0 otherwise. Tag
 * the branch with an UNLIKELY tag, as to provide an optimization hint
 * to the compiler.
 */
#define FAILED_UNLIKELY(x) (FAILED(x))

/**
 * \brief Generate a new aresult_t code.
 * \param err nonzero indicates an error, 0 indicates no error
 * \param warn nonzero indicates a warning, 0 indicates no warning
 * \param facil facility ID code
 * \param code error code
 */
#define ARESULT_CODE(err, warn, facil, code) \
    ((aresult_t)(( (err) ? 1ul << ARESULT_ERROR_BIT : 0 ) | \
     ( (warn) ? 1ul << ARESULT_WARNING_BIT : 0 ) | \
     TSL_DEPOSIT((facil), ARESULT_FACILITY_OFFSET, ARESULT_FACILITY_SIZE) | \
     TSL_DEPOSIT((code), ARESULT_CODE_OFFSET, ARESULT_CODE_SIZE)))

/**
 * \brief Macro to generate an error-indicating aresult_t
 * \param facil facility ID code
 * \param code error code
 */
#define ARESULT_ERROR(facil, code) ARESULT_CODE(1, 0, (facil), (code))

/* List of pre-defined system facilities */
#define FACIL_SYSTEM            0x0 /** Generic system error codes */
