#include <test/malloc_helpers.h>

#include <tsl/errors.h>
#include <tsl/cal.h>
#include <tsl/diag.h>
#include <tsl/time.h>

#include <stdbool.h>
#include <stdlib.h>

/**
 * Will the real libc malloc(3) please stand up...
 */
extern void *__libc_malloc(size_t size);

enum test_malloc_mode {
    TEST_MALLOC_MODE_NORMAL,
    TEST_MALLOC_MODE_FAIL_AFTER_COUNTDOWN,
    TEST_MALLOC_MODE_RANDOM_FAILURE,
};

static
enum test_malloc_mode _test_hook_mode = TEST_MALLOC_MODE_NORMAL;

static
unsigned _test_hook_countdown = 0;

aresult_t test_malloc_set_sporadic_failure(void)
{
    aresult_t ret = A_OK;

    DIAG("TEST: Setting malloc(3) hook to fail sporadically.");

    _test_hook_mode = TEST_MALLOC_MODE_RANDOM_FAILURE;
    _test_hook_countdown = 0;

    return ret;
}

aresult_t test_malloc_set_countdown_failure(unsigned counter)
{
    aresult_t ret = A_OK;

    DIAG("TEST: Setting malloc(3) hook to fail after a countdown of %u", counter);

    _test_hook_mode = TEST_MALLOC_MODE_FAIL_AFTER_COUNTDOWN;
    _test_hook_countdown = counter;

    return ret;
}

aresult_t test_malloc_disable_failures(void)
{
    aresult_t ret = A_OK;

    DIAG("TEST: Disabling malloc(3) hook.");

    _test_hook_mode = TEST_MALLOC_MODE_NORMAL;
    _test_hook_countdown = 0;

    return ret;
}

static
void *_test_malloc_random(size_t size)
{
    return NULL;
}

/**
 * Override the libc-provided malloc with our evil malloc twin. Normally, this will just
 * pass allocations through to libc's malloc, but with various flags being set, we can
 * have certain types of failures occur.
 */
void *malloc(size_t size)
{
    void *out = NULL;

    switch (_test_hook_mode) {
    case TEST_MALLOC_MODE_RANDOM_FAILURE:
        out = _test_malloc_random(size);
        break;
    case TEST_MALLOC_MODE_FAIL_AFTER_COUNTDOWN:
        if (0 == _test_hook_countdown) {
            break;
        } else {
            _test_hook_countdown--;
        }
    case TEST_MALLOC_MODE_NORMAL:
    default:
        out = __libc_malloc(size);
    }

    return out;
}

