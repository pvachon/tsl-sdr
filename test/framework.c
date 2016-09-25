#include <test/framework.h>
#include <test/framework_priv.h>
#include <test/malloc_helpers.h>

#include <tsl/assert.h>
#include <tsl/cal.h>
#include <tsl/diag.h>
#include <tsl/safe_alloc.h>
#include <tsl/safe_string.h>
#include <tsl/sections.h>

#include <string.h>

#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

CAL_THREAD_LOCAL
int _tf_tl_tid = -1;

static
int _tf_gettid(void)
{
    if (_tf_tl_tid == -1) {
        _tf_tl_tid = (int)syscall(SYS_gettid);
    }

    return _tf_tl_tid;
}

static
void string_cleanup(char **pstr)
{
    if (NULL == pstr || NULL == *pstr) {
        return;
    }

    TFREE(*pstr);
}

static
const char *_tf_debug_lev[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "(UNKNOWN)",
};

extern
bool _test_enable_sporadic_malloc_failures;

void tf_print_log(int level, char *file, int line, char *fmt, ...)
{
    va_list va;
    char *fmtr CAL_CLEANUP(string_cleanup) = NULL;
    const char *level_str = NULL;

    level_str = (level < 0 || level > TEST_LOG_LEVEL_ERROR) ? "(UNKNOWN)" : _tf_debug_lev[level];

    if (FAILED(tasprintf(&fmtr, "TEST:%s[%d]: %s (%s:%d)\n", level_str, _tf_gettid(), fmt, file, line))) {
        DIAG("ERROR: unable to generate print formatting string...");
        goto done;
    }

    va_start(va, fmt);

    vfprintf(stderr, fmtr, va);

    va_end(va);

done:
    return;
}

static
aresult_t _tf_execute_test_case(struct test_suite *suite, struct test_suite_unit *unit)
{
    aresult_t ret = A_OK;
    aresult_t unit_ret = A_OK;

    TSL_ASSERT_ARG(NULL != suite);
    TSL_ASSERT_ARG(NULL != unit);

    printf("%s - %30s: ", suite->name, unit->name);

    if (NULL == unit->test) {
        printf("no actual test specified!\n");
        ret = A_E_INVAL;
        goto done;
    }

    if (NULL != suite->before && FAILED(ret = suite->before())) {
        printf("failed to do pre-unit setup\n");
        ret = A_E_INVAL;
        goto done;
    }

    if (true == _test_enable_sporadic_malloc_failures) {
        TSL_BUG_IF_FAILED(test_malloc_set_sporadic_failure());
    }

    if (FAILED(unit_ret = unit->test())) {
        printf("FAILED.\n");
    } else {
        printf("OK.\n");
    }

    TSL_BUG_IF_FAILED(test_malloc_disable_failures());

    if (NULL != suite->after && FAILED(ret = suite->after())) {
        printf("failed to do post-unit cleanup\n");
        ret = A_E_INVAL;
        goto done;
    }

    /* Even if we succeeded at cleanup, if the unit failed, mark it as having failed. */
    if (FAILED(unit_ret)) {
        ret = unit_ret;
    }

done:
    return ret;
}

static
aresult_t _tf_execute_test_suite(struct test_suite *suite)
{
    aresult_t ret = A_OK;

    struct test_suite_unit **suite_start = NULL;
    struct test_suite_unit **suite_stop = NULL;
    struct test_suite_unit **cursor = NULL;
    struct test_suite_unit *unit = NULL;

    int run = 0;
    int failed = 0;

    TSL_ASSERT_ARG(NULL != suite);

    printf("[Test Suite %s]\n", suite->name);

    suite_start = suite->start_units;
    suite_stop = suite->end_units;

    if (NULL != suite->setup) {
        if (FAILED(ret = suite->setup())) {
            printf("failed to prepare for test suite.\n");
            goto done;
        }
    }

    for (cursor = suite_start; cursor != suite_stop; cursor++) {
        unit = *cursor;
        run++;

        if (FAILED(_tf_execute_test_case(suite, unit))) {
            failed++;
        }
    }

    if (NULL != suite->cleanup) {
        if (FAILED(ret = suite->cleanup())) {
            printf("failed to clean up after the test suite.\n");
            goto done;
        }
    }

    printf("[Test Suite %s] - Finished %d/%d tests passed.\n", suite->name, run - failed, run);

done:
    if (0 != failed) {
        ret = A_E_INVAL;
    }

    return ret;
}

static
aresult_t _tf_check_unit_in_list(const char **unit_list, const char *unit, bool *pfound)
{
    aresult_t ret = A_OK;

    const char **ptr = NULL;

    TSL_ASSERT_ARG(NULL != unit_list);
    TSL_ASSERT_ARG(NULL != unit);

    *pfound = false;
    ptr = unit_list;

    while (NULL != *ptr) {
        if (0 == strcmp(*ptr, unit)) {
            *pfound = true;
            break;
        }
        ptr++;
    }

    return ret;
}

aresult_t tf_execute_all_test_suites(const char **unit_list)
{
    aresult_t ret = A_OK;

    struct test_suite *suite = NULL;

    CR_FOR_EACH_LOADABLE(suite, _all_test_suites) {
        if (NULL != unit_list) {
            bool found = NULL;
            TSL_BUG_IF_FAILED(_tf_check_unit_in_list(unit_list, suite->name, &found));
            if (false == found) {
                DIAG("Skipping unit '%s', it is not in our list to execute.", suite->name);
                continue;
            }
        }

        if (FAILED(_tf_execute_test_suite(suite))) {
            ret = A_E_INVAL;
        }
    }

    return ret;
}
