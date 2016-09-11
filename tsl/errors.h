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
#include <tsl/result.h>

/**
 * \group aresult_errors aresult_t Return Codes
 * All valid error codes for generic system facilities (such as the TSL and low-level
 * handler code).
 * @{
 */
/* TODO: these should either consistently have underscores or not */
/** Everything is OK */
#define A_OK            ARESULT_CODE(0, 0, FACIL_SYSTEM, 0)
/** Out of memory */
#define A_E_NOMEM       ARESULT_ERROR(FACIL_SYSTEM, 1)
/** Bad arguments */
#define A_E_BADARGS     ARESULT_ERROR(FACIL_SYSTEM, 2)
/** Not found */
#define A_E_NOTFOUND    ARESULT_ERROR(FACIL_SYSTEM, 3)
/** Busy/In Use */
#define A_E_BUSY        ARESULT_ERROR(FACIL_SYSTEM, 4)
/** Invalid reference */
#define A_E_INVAL       ARESULT_ERROR(FACIL_SYSTEM, 5)
/** Thread not found */
#define A_E_NOTHREAD    ARESULT_ERROR(FACIL_SYSTEM, 6)
/** Target is empty */
#define A_E_EMPTY       ARESULT_ERROR(FACIL_SYSTEM, 7)
/** Invalid socket */
#define A_E_NO_SOCKET   ARESULT_ERROR(FACIL_SYSTEM, 8)
/** Invalid entity */
#define A_E_NOENT       ARESULT_ERROR(FACIL_SYSTEM, 9)
/** Invalid date */
#define A_E_INV_DATE    ARESULT_ERROR(FACIL_SYSTEM, 10)
/** No space remains */
#define A_E_NOSPC       ARESULT_ERROR(FACIL_SYSTEM, 11)
/** Item already exists */
#define A_E_EXIST       ARESULT_ERROR(FACIL_SYSTEM, 12)
/** Unknown message or data type */
#define A_E_UNKNOWN     ARESULT_ERROR(FACIL_SYSTEM, 13)
/** Process is finished */
#define A_E_DONE        ARESULT_ERROR(FACIL_SYSTEM, 14)
/** An integer overflow of a parameter or parameters has occurred */
#define A_E_OVERFLOW    ARESULT_ERROR(FACIL_SYSTEM, 15)
/** Buffer or structure cannot accept any more data */
#define A_E_FULL        ARESULT_ERROR(FACIL_SYSTEM, 16)
/** End of File */
#define A_E_EOF         ARESULT_ERROR(FACIL_SYSTEM, 17)
/** Rejected by filter */
#define A_E_REJECTED    ARESULT_ERROR(FACIL_SYSTEM, 18)
/** Timeout */
#define A_E_TIMEOUT    ARESULT_ERROR(FACIL_SYSTEM, 19)

/**
 * Returns a string for the given TSL result.
 */
const char *tsl_result_to_string(aresult_t result);

/**
 * Returns a friendly string for the given TSL result.
 */
const char *tsl_result_to_string_friendly(aresult_t result);

/*@}*/
