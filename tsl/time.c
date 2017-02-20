/*
 *  time.c - Epoch Timestamp calculation and retrieval
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

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
#include <tsl/time.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>

#include <stdint.h>

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_PROCFS_READ_SIZE                4096ul

#define IS_NUM(_c)              ((_c) >= '0' && (_c) <= '9')
#define IS_WHITE_SPACE(_c)      ((_c) == '\t' || (_c) == '\n' || (_c) == '\r' || (_c) == ' ')

/**
 * The clock frequency, in hertz, at which the TSC incrments.
 */
uint64_t _tsl_clock_freq = 0;

/**
 * The TSC value at the start of the process' epoch (i.e. at start time)
 */
uint64_t _tsl_proc_clock_offset = 0;

#ifdef __x86_64__
/**
 * The clock coefficient, used to efficiently compute the time in nanoseconds since the start
 * of the program.
 */
uint64_t _tsl_clock_coeff = 0;

/**
 * Calculate the clock frequency of the monotonically increasing timestamp counter for the given
 * machine.
 *
 * \note Yes, seriously, this is the best way to get the clock frequency. I'm sorry.
 */
static
aresult_t tsl_calc_clockfreq(uint64_t *clock_hz)
{
    aresult_t ret = A_E_INVAL;

    TSL_ASSERT_ARG(NULL != clock_hz);

    enum clockfreq_parse_state {
        STATE_BEFORE_COLON,
        STATE_AFTER_COLON,
        STATE_NUMBER_BEFORE_DECIMAL,
        STATE_NUMBER_AFTER_DECIMAL,
        STATE_DONE,
    };

    int fd = -1;
    ssize_t nread = -1,
            max_len = 0;

    char *cpuinfo = NULL,
         *mhz_ptr = NULL;

    uint64_t hz = 0,
             scale = 1000000;

    enum clockfreq_parse_state st = STATE_BEFORE_COLON;

    if (NULL == clock_hz) {
        goto done;
    }

    TSL_BUG_IF_FAILED(TCALLOC((void **)&cpuinfo, 1ul, MAX_PROCFS_READ_SIZE));

    if (0 > (fd = open("/proc/cpuinfo", O_RDONLY))) {
        PDIAG("Failed to open /proc/cpuinfo. Aborting.");
        goto done;
    }

    if (0 > (nread = read(fd, cpuinfo, MAX_PROCFS_READ_SIZE))) {
        PDIAG("read: failed to read %lu bytes from /proc/cpuinfo", MAX_PROCFS_READ_SIZE);
        goto done;
    }

    if (NULL == (mhz_ptr = memmem(cpuinfo, nread, "cpu MHz", 7))) {
        DIAG("Could not find cpu MHz section of /proc/cpuinfo");
        goto done;
    }

    max_len = nread - ((intptr_t)mhz_ptr - (intptr_t)cpuinfo);

    if (max_len < 0) {
        DIAG("max_len < 0, aborting.");
        goto done;
    }

    for (size_t i = 0; i < max_len; i++) {
        if (IS_NUM(mhz_ptr[i])) {
            if (!(st == STATE_AFTER_COLON || st == STATE_NUMBER_BEFORE_DECIMAL || st == STATE_NUMBER_AFTER_DECIMAL)) {
                DIAG("malformed procfs entry: CPU frequency numbers state is a mess.");
                break;
            } else if (st == STATE_AFTER_COLON) {
                st = STATE_NUMBER_BEFORE_DECIMAL;
            }
            hz = (hz * 10) + (mhz_ptr[i] - '0');

            if (st == STATE_NUMBER_AFTER_DECIMAL) {
                scale /= 10;

                if (scale == 1) {
                    /* Early exit -- precision in the CPU frequency number is greater than necessary */
                    st = STATE_DONE;
                    break;
                }
            }
        } else if (mhz_ptr[i] == '.') {
            if (st != STATE_NUMBER_BEFORE_DECIMAL) {
                DIAG("malformed procfs entry: extraneous decimal in integer frequency value.");
                break;
            }
            st = STATE_NUMBER_AFTER_DECIMAL;
        } else if (mhz_ptr[i] == ':') {
            if (st != STATE_BEFORE_COLON) {
                DIAG("malformed procfs entry: extraneous colon in string.");
                break;
            }
            st = STATE_AFTER_COLON;
        } else if (IS_WHITE_SPACE(mhz_ptr[i])) {
            if (st == STATE_NUMBER_BEFORE_DECIMAL || st == STATE_NUMBER_AFTER_DECIMAL) {
                st = STATE_DONE;
                break;
            }
        } else {
            if (st != STATE_BEFORE_COLON) {
                DIAG("malformed procfs entry: unexpected value");
                goto done;
            }
            /* Otherwise, continue iteration, eating it up */
        }
    }

    if (st != STATE_DONE) {
        DIAG("Unexpected end of input, aborting");
    }

    *clock_hz = scale * hz;

    ret = A_OK;
done:
    TFREE(cpuinfo);

    if (fd >= 0) {
        close(fd);
    }

    return ret;
}
#endif /* x86_64 */

static
void system_get_timestamp(uint32_t *ts_sec, uint32_t *ts_nsec_frac)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *ts_sec = ts.tv_sec;
    *ts_nsec_frac = ts.tv_nsec;
}

/* Public interface */
aresult_t time_get_time_frac(uint32_t *ts_sec, uint32_t *ts_nsec_frac)
{
    TSL_ASSERT_ARG_DEBUG(NULL != ts_sec);
    TSL_ASSERT_ARG_DEBUG(NULL != ts_nsec_frac);

    system_get_timestamp(ts_sec, ts_nsec_frac);

    return A_OK;
}

uint64_t time_get_time(void)
{
    uint32_t ts_sec = 0, ts_nsec_frac = 0;
    system_get_timestamp(&ts_sec, &ts_nsec_frac);
    return ((uint64_t)ts_sec * 1000000000ull) + ts_nsec_frac;
}

aresult_t tsl_time_init(void)
{
    aresult_t ret = A_OK;

#ifdef __x86_64__
    _tsl_proc_clock_offset = __tsl_get_cpu_timer();

    if (tsl_calc_clockfreq(&_tsl_clock_freq)) {
        PANIC("Failed to calculate CPU clock frequency.");
    }

    DIAG("CPU Core Clock: %f MHz", (float)_tsl_clock_freq/1e6);


    __uint128_t denom = ((__uint128_t)1) << 64;
    denom *= 1000000000ull;
    _tsl_clock_coeff = denom /(__uint128_t)_tsl_clock_freq;
#endif

    return ret;
}

