/*
 *  safe_alloc.c - Wrappers around standard library memory functions,
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
#include <tsl/safe_alloc.h>

#include <tsl/cal.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

#include <stdlib.h>
#include <string.h>

#define ALC_MSG(sev, sys, msg, ...) MESSAGE("SAFEALLOC", sev, sys, msg, ##__VA_ARGS__)

CAL_CHECKED aresult_t __safe_malloc(void **ptr, size_t size)
{
    aresult_t ret = A_OK;

    void *n = NULL;

    TSL_ASSERT_ARG(NULL != ptr);
    TSL_ASSERT_ARG(0 != size);

    *ptr = NULL;

    if (NULL == (n = malloc(size))) {
        ALC_MSG(SEV_FATAL, "OUT-OF-MEMORY", "No more heap space available for allocation of %zu bytes", size);
        ret = A_E_NOMEM;
        goto done;
    }

    *ptr = n;

done:
    return ret;
}

CAL_CHECKED aresult_t __safe_calloc(void **ptr, size_t count, size_t size)
{
    aresult_t ret = A_OK;

    void *n = NULL;

    TSL_ASSERT_ARG(NULL != ptr);
    TSL_ASSERT_ARG(0 != size);
    TSL_ASSERT_ARG(0 != count);

    *ptr = NULL;

    if (NULL == (n = calloc(count, size))) {
        ALC_MSG(SEV_FATAL, "OUT-OF-MEMORY", "No more heap space available for allocation of %zu x %zu bytes", count, size);
        ret = A_E_NOMEM;
        goto done;
    }

    *ptr = n;

done:
    return ret;
}

CAL_CHECKED aresult_t __safe_realloc(void **ptr, size_t size)
{
    aresult_t ret = A_OK;

    void *n = NULL;

    TSL_ASSERT_ARG(NULL != ptr);
    TSL_ASSERT_ARG(0 != size);

    if (NULL == (n = realloc(*ptr, size))) {
        ALC_MSG(SEV_FATAL, "OUT-OF-MEMORY", "No more heap space available for allocation of %zu bytes", size);
        ret = A_E_NOMEM;
        goto done;
    }

    *ptr = n;

done:
    return ret;
}

#define HALF_SIZE_T         ((size_t)1 << ((sizeof(size_t) << 3) - 1))

CAL_CHECKED
aresult_t __safe_aligned_zalloc(void **pptr, size_t size, size_t count, size_t align)
{
    aresult_t ret = A_OK;

    size_t alloc_size = 0;
    void *ptr = NULL;

    TSL_ASSERT_ARG(NULL != pptr);
    TSL_ASSERT_ARG(0 < size);
    TSL_ASSERT_ARG(0 < count);

    *pptr = NULL;

    alloc_size = size * count;

    /* Check for an overflow in size * count */
    if (CAL_UNLIKELY(size > HALF_SIZE_T || count > HALF_SIZE_T)) {
        if (alloc_size / size != count) {
            ret = A_E_OVERFLOW;
            goto done;
        }
    }

    /* We're good, so let's use posix_memalign to get an aligned pointer */
    if (0 != posix_memalign(&ptr, align, alloc_size)) {
        ret = A_E_NOMEM;
        goto done;
    }

    /* Zero the memory behind the pointer */
    memset(ptr, 0, alloc_size);

    *pptr = ptr;

done:
    return ret;
}

void free_u32_array(uint32_t **ptr)
{
    TFREE(*ptr);
}

void free_i16_array(int16_t **ptr)
{
    TFREE(*ptr);
}

void free_double_array(double **ptr)
{
    TFREE(*ptr);
}

void free_memory(void **ptr)
{
    TFREE(*ptr);
}

void free_string(char **str)
{
    TFREE(*str);
}
