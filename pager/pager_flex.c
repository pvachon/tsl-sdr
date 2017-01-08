/*
 *  pager_flex.c - FLEX protocol handler
 *
 *  Copyright (c)2016 Phil Vachon <phil@security-embedded.com>
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

#include <pager/pager.h>
#include <pager/pager_priv.h>
#include <pager/pager_flex.h>
#include <pager/bch_code.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

aresult_t pager_flex_new(struct pager_flex **pflex, uint32_t freq_hz, pager_flex_on_message_cb_t on_msg)
{
    aresult_t ret = A_OK;

    struct pager_flex *flex = NULL;

    TSL_ASSERT_ARG(NULL != pflex);
    TSL_ASSERT_ARG(NULL != on_msg);

    if (FAILED(ret = TZAALLOC(flex, SYS_CACHE_LINE_LENGTH))) {
        goto done;
    }

    flex->freq_hz = freq_hz;
    flex->on_msg = on_msg;

    *pflex = flex;

done:
    return ret;
}

aresult_t pager_flex_delete(struct pager_flex **pflex)
{
    aresult_t ret = A_OK;

    struct pager_flex *flex = NULL;

    TSL_ASSERT_ARG(NULL != pflex);
    TSL_ASSERT_ARG(NULL != *pflex);

    flex = *pflex;

    /* Cleanup the flex demod context */

    TFREE(flex);
    *pflex = NULL;

    return ret;
}

aresult_t pager_flex_on_pcm(struct pager_flex *flex, const int16_t *pcm_samples, size_t nr_samples)
{
    aresult_t ret = A_OK;

    return ret;
}

