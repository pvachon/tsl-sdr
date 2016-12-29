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

#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifdef _TSL_DEBUG
#include <tsl/time.h>
#include <tsl/diag.h>

#include <string.h>
#include <errno.h>

/** \brief Emit a diagnostic message
 * Emit a formatted diagnostic message during testing so that state of the system
 * can be tracked readily.
 */
#ifndef _TSL_NO_DIAG_TIMESTAMPS

#define DIAG(x, ...) \
    do {                                                                                                                                        \
        uint32_t hi = 0, lo = 0;                                                                                                                \
        time_get_time_frac(&hi, &lo);                                                                                                           \
        fprintf(stderr, "[%9u.%09u] [tid=%5d] DIAG: " x " (%s:%d, %s)\n", hi, lo, (int)syscall(SYS_gettid), ##__VA_ARGS__, __FILE__, __LINE__, __FUNCTION__);  \
        fflush(stderr);                                                                                                                         \
    } while (0)

#else

#define DIAG(x, ...) \
    do {                                                                                                                                        \
        fprintf(stderr, "DIAG[%d]: " x " (%s:%d, %s)\n", (int)syscall(SYS_gettid), ##__VA_ARGS__, __FILE__, __LINE__, __FUNCTION__);  \
        fflush(stderr);                                                                                                                         \
    } while (0)

#endif /* ndef _TSL_NO_DIAG_TIMESTAMPS */

#define PDIAG(x, ...) \
    do {                                                    \
        int __errnum = errno;                               \
        DIAG(x " (%d: %s)", ##__VA_ARGS__, __errnum, strerror(__errnum));   \
    } while (0)

#else
/* Diag output is eaten if building in production */
#define DIAG(...)
#define PDIAG(...)
#endif /* defined(_TSL_DEBUG) */

#define FDIAG(f, x, ...) \
    do {                                                                                                                                                \
        if (NULL != f) {                                                                                                                                  \
            uint32_t hi = 0, lo = 0;                                                                                                                \
            time_get_time_frac(&hi, &lo);                                                                                                           \
            fprintf(f, "[%9u.%09u] [tid=%5d] DIAG: " x " (%s:%d, %s)\n", hi, lo, (int)syscall(SYS_gettid), ##__VA_ARGS__, __FILE__, __LINE__, __FUNCTION__);  \
        }                                                                                                                                               \
    } while (0)

#define SEV_SUCCESS     "S"
#define SEV_INFO        "I"
#define SEV_WARNING     "W"
#define SEV_ERROR       "E"
#define SEV_FATAL       "F"

/**
 * A message that is always displayed when the application is running -- typically
 * a particular subsystem will have its own notification macros that will be used
 * instead of MESSAGE directly.
 * \param subsys The subsystem name (all caps)
 * \param severity A severity code
 * \param ident The identifier of the error (no spaces)
 * \param message A human-readable message
 * \note Line number and function name are also output as a part of this function
 */
#if defined(_TSL_DEBUG) && !defined(_TSL_NO_DIAG_TIMESTAMPS)

#define MESSAGE(subsys, severity, ident, message, ...) \
    do {                            \
        uint32_t hi = 0, lo = 0;                                                                                                                \
        time_get_time_frac(&hi, &lo);                                                                                                           \
        fprintf(stderr, "[%9u.%09u] [tid=%5d] %%" subsys "-" severity "-" ident ", " message " (%s:%d in %s)\n", hi, lo, (int)syscall(SYS_gettid), ##__VA_ARGS__, __FILE__, __LINE__, __FUNCTION__); \
    } while (0)

#else

#define MESSAGE(subsys, severity, ident, message, ...) \
    do {                            \
        fprintf(stderr, "%%" subsys "-" severity "-" ident ", " message " (%s:%d in %s)\n", ##__VA_ARGS__, __FILE__, __LINE__, __FUNCTION__); \
    } while (0)
#endif /* defined(_TSL_DEBUG) && undefined(_TSL_NO_DIAG_TIMESTAMPS) */

