#include <filter/sample_buf.h>

#include <tsl/result.h>
#include <tsl/diag.h>
#include <tsl/assert.h>

#include <stdatomic.h>

aresult_t sample_buf_decref(struct sample_buf *buf)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != buf);
    /* Decrement the reference count */
    if (1 == atomic_fetch_sub(&buf->refcount, 1)) {
        TSL_BUG_ON(NULL == buf->release);
        TSL_BUG_IF_FAILED(buf->release(buf));
        DIAG("FREED: %p", buf);
    }

    return ret;
}

