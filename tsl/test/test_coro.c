#include <tsl/coroutine.h>

#include <tsl/test/helpers.h>

#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

static
const char *_coro_state_magic = "hello, world!";

static
aresult_t _coro_test_fiber_trivial(struct coro_ctx *ctx, void *state)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != ctx);
    TSL_ASSERT_ARG(NULL != state);

    DIAG("Hello from the trivial coroutine fiber");

    if (state != (void *)_coro_state_magic) {
        DIAG("Magic state: %p provided state: %p", _coro_state_magic, state);
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

static
aresult_t _coro_test_fiber_yield(struct coro_ctx *ctx, void *state)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != ctx);
    TSL_ASSERT_ARG(NULL != state);

    if (state != (void *)_coro_state_magic) {
        DIAG("Magic state: %p provided state: %p", _coro_state_magic, state);
        ret = A_E_INVAL;
        goto done;
    }

    for (int i = 0; i < 10; i++) {
        DIAG("Yield #%d", i);
        coro_yield(ctx);
    }

done:
    return ret;
}

TEST_DECL(test_coro)
{
    struct coro_thread_state *st;
    struct coro_ctx test_ctx;

    TEST_ASSERT_OK(coro_thread_get(&st));

    DIAG("CORO: Test simple straight-through fiber.");
    TEST_ASSERT_OK(coro_ctx_init(&test_ctx, 8192, _coro_test_fiber_trivial, (void *)_coro_state_magic));

    TEST_ASSERT_OK(coro_start(st, &test_ctx));
    TEST_ASSERT_OK(coro_ctx_release(&test_ctx));

    DIAG("CORO: Test multiple-yield fiber.");
    TEST_ASSERT_OK(coro_ctx_init(&test_ctx, 8192, _coro_test_fiber_yield, (void *)_coro_state_magic));

    TEST_ASSERT_EQUALS(coro_start(st, &test_ctx), CORO_CONTINUE);

    for (int i = 0; i < 9; i++) {
        TEST_ASSERT_EQUALS(coro_ctx_swap(st, &test_ctx), CORO_CONTINUE);
    }

    /* Last context swap in before the coroutine terminates */
    TEST_ASSERT_OK(coro_ctx_swap(st, &test_ctx));

    TEST_ASSERT_OK(coro_ctx_release(&test_ctx));

    return TEST_OK;
}
