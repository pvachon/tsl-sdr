#include <pager/pager.h>
#include <pager/pager_priv.h>
#include <pager/pager_flex.h>

#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/safe_alloc.h>

#include <stdint.h>

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

