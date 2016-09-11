#include <app/app.h>

#include <test/framework.h>
#include <test/assert.h>

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

static
aresult_t _app_setup(void)
{
    return A_OK;
}

static
aresult_t _app_cleanup(void)
{
    return A_OK;
}

static
volatile bool _app_sigusr2_flag1 = false;

static
aresult_t _app_sigusr2_handler1(void)
{
    /* Probably wouldn't do it this way in a real program, race condition and all... */
    _app_sigusr2_flag1 = !_app_sigusr2_flag1;
    return A_OK;
}

static
volatile bool _app_sigusr2_flag2 = false;

static
aresult_t _app_sigusr2_handler2(void)
{
    _app_sigusr2_flag2 = true;
    return A_OK;
}

TEST_DECLARE_UNIT(sigusr2, test_app)
{
    struct app_sigusr2_state_t handler_state1;
    struct app_sigusr2_state_t handler_state2;

    /* Can't have a NULL handler state */
    TEST_ASSERT_FAILED(app_sigusr2_catch(NULL));

    /* Can't have a NULL handler */
    handler_state1.handler = NULL;
    TEST_ASSERT_FAILED(app_sigusr2_catch(&handler_state1));

    /* Setup the handler */
    handler_state1.handler = _app_sigusr2_handler1;
    TEST_ASSERT_OK(app_sigusr2_catch(&handler_state1));

    /* Trigger the handler */
    TEST_ASSERT_EQUALS(_app_sigusr2_flag1, false);
    TEST_ASSERT_EQUALS(_app_sigusr2_flag2, false);
    kill(getpid(), SIGUSR2);
    TEST_ASSERT_EQUALS(_app_sigusr2_flag1, true);
    TEST_ASSERT_EQUALS(_app_sigusr2_flag2, false);

    /* Setup the second handler */
    handler_state2.handler = _app_sigusr2_handler2;
    TEST_ASSERT_OK(app_sigusr2_catch(&handler_state2));
    kill(getpid(), SIGUSR2);
    TEST_ASSERT_EQUALS(_app_sigusr2_flag1, false);
    TEST_ASSERT_EQUALS(_app_sigusr2_flag2, true);

    return A_OK;
}

TEST_DECLARE_SUITE(test_app, _app_cleanup, _app_setup, NULL, NULL);
