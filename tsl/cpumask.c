/*
 *  cpumask.c - CPU Set Management
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
#include <tsl/cpumask.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/safe_alloc.h>

#include <config/engine.h>

#include <stdlib.h>

#include <sched.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Abstraction on top of Linux CPU thread masks */
struct cpu_mask {
    cpu_set_t *mask;
    long num_cpus;
    long size;
};

aresult_t cpu_mask_new(struct cpu_mask **mask)
{
    aresult_t ret = A_OK;
    struct cpu_mask *msk = NULL;

    TSL_ASSERT_ARG(mask != NULL);
    *mask = NULL;

    if (FAILED(ret = TZALLOC(msk))) {
        goto done;
    }

    msk->num_cpus = sysconf(_SC_NPROCESSORS_CONF);

    DIAG("Creating a CPU Set Mask for %ld CPUs.", msk->num_cpus);

    msk->mask = CPU_ALLOC(msk->num_cpus);

    if (!msk->mask) {
        ret = A_E_NOMEM;
        goto done_fail;
    }

    msk->size = CPU_ALLOC_SIZE(msk->num_cpus);
    CPU_ZERO_S(msk->size, msk->mask);

    *mask = msk;

done_fail:
    if (FAILED(ret)) {
        if (NULL != msk) {
            TSL_BUG_IF_FAILED(cpu_mask_delete(&msk));
        }
    }

done:
    return ret;
}

aresult_t cpu_mask_clear(struct cpu_mask *mask, size_t cpu_id)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(mask != NULL);

    CPU_CLR_S(cpu_id, mask->size, mask->mask);

    return ret;
}

aresult_t cpu_mask_set(struct cpu_mask *mask, size_t cpu_id)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(mask != NULL);

    CPU_SET_S(cpu_id, mask->size, mask->mask);

    return ret;
}

aresult_t cpu_mask_clear_all(struct cpu_mask *mask)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(mask != NULL);

    CPU_ZERO_S(mask->size, mask->mask);

    return ret;

}

aresult_t cpu_mask_set_all(struct cpu_mask *mask)
{
    aresult_t ret = A_E_INVAL;

    TSL_ASSERT_ARG(mask != NULL);

    return ret;
}

aresult_t cpu_mask_clone(struct cpu_mask **_new,
                         const struct cpu_mask *orig)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(_new != NULL);
    *_new = NULL;
    TSL_ASSERT_ARG(orig != NULL);

    /* TODO */
    PANIC("cpu_mask_clone not implemented");

    return ret;
}

aresult_t cpu_mask_delete(struct cpu_mask **mask)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_PTR_BY_REF(mask);

    struct cpu_mask *msk = *mask;

    if (msk->mask) {
        CPU_FREE(msk->mask);
    }
    TFREE(*mask);

    return ret;

}

aresult_t cpu_mask_test(struct cpu_mask *mask,
                        size_t cpu_id,
                        int *value)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(mask != NULL);
    TSL_ASSERT_ARG(value != NULL);

    *value = CPU_ISSET_S(cpu_id, mask->size, mask->mask);

    return ret;
}

aresult_t cpu_mask_apply(struct cpu_mask *mask)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(mask);

    if (sched_setaffinity(0, mask->size, mask->mask) != 0) {
        DIAG("sched_setaffinity: failure. %d - %s", errno, strerror(errno));
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}
