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

