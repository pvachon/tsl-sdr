/*
 *  safe_string.c - Wrappers around standard library string functions,
 *          with some error checks.
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

#include <tsl/safe_string.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/cal.h>

#include <stdarg.h>
#include <string.h>
#include <stdio.h>

CAL_CHECKED
aresult_t tstrdup(char **dst, const char *src)
{
    aresult_t ret = A_OK;

    char *tgt = NULL;

    TSL_ASSERT_ARG(NULL != dst);
    TSL_ASSERT_ARG(NULL != src);

    *dst = NULL;

    if (NULL == (tgt = strdup(src))) {
        ret = A_E_NOMEM;
        goto done;
    }

    *dst = tgt;

done:
    return ret;
}

CAL_CHECKED
aresult_t tasprintf(char **dst, const char *fmt, ...)
{
    aresult_t ret = A_OK;

    va_list ap;
    int a_ret = 0;

    TSL_ASSERT_ARG(NULL != dst);
    TSL_ASSERT_ARG(NULL != fmt);

    *dst = NULL;

    va_start(ap, fmt);
    a_ret = vasprintf(dst, fmt, ap);
    va_end(ap);

    if (0 >= a_ret) {
        *dst = NULL;
        ret = A_E_NOMEM;
        goto done;
    }

done:
    return ret;
}

