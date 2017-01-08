/*
 *  parsers.c - Helpers for parsing magnitudes
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

#include <tsl/parsers.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <string.h>
#include <stdlib.h>

aresult_t tsl_parse_mem_bytes(const char *str, uint64_t *val)
{
    aresult_t ret = A_OK;
    char *endptr = NULL;
    uint64_t memval = 0;

    TSL_ASSERT_ARG(NULL != str);
    TSL_ASSERT_ARG(NULL != val);

    memval = strtoull(str, &endptr, 0);

    switch (*endptr) {
    case 'E':
    case 'e':
        memval <<= 10;
    case 'P':
    case 'p':
        memval <<= 10;
    case 'T':
    case 't':
        memval <<= 10;
    case 'G':
    case 'g':
        memval <<= 10;
    case 'M':
    case 'm':
        memval <<= 10;
    case 'K':
    case 'k':
        memval <<= 10;
        endptr++;
    default:
        break;
    }

    *val = memval;

    return ret;
}

aresult_t tsl_parse_time_interval(const char *str, uint64_t *val)
{
    aresult_t ret = A_OK;

    char *endptr = NULL;
    uint64_t n = 0;

    TSL_ASSERT_ARG(NULL != str);
    TSL_ASSERT_ARG(NULL != val);

    n = strtoull(str, &endptr, 0);

    /* If n is 0 or the endptr points to a null terminator, we're done our work. */
    if (0 == n || *endptr == '\0') {
        goto done;
    }

    switch (*endptr++) {
    case 'n':
        break;
    case 'u':
        n *= 1000ull;
        break;
    case 'm':
        n *= 1000000ull;
        break;
    case 's':
        n *= 1000000000ull;
        break;
    default:
        ret = A_E_INVAL;
        goto done;
    }

    if ('\0' != *endptr || 's' != *endptr) {
        ret = A_E_INVAL;
        goto done;
    }

    *val = n;

done:
    return ret;
}
