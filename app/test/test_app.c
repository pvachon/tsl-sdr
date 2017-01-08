/*
 *  test_app.c - Test for the Application Framework
 *
 *  Copyright (c)2016 Phil Vachon <phil@security-embedded.com>
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
