/*
 *  main.c - Entry point and runner for the TSL test framework.
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL) Test Framework
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

#include <test/framework.h>
#include <test/framework_priv.h>

#include <app/app.h>

#include <config/engine.h>

#include <tsl/safe_alloc.h>
#include <tsl/errors.h>
#include <tsl/diag.h>
#include <tsl/assert.h>
#include <tsl/safe_string.h>
#include <tsl/version.h>

#include <string.h>
#include <getopt.h>
#include <stdlib.h>

#define TEST_MSG(sev, mnem, msg, ...) \
    MESSAGE("TEST", sev, mnem, msg, ##__VA_ARGS__)

static
bool _list_units = false;

static
const char **_units_to_execute = NULL;

static
size_t _nr_units_to_exec = 0;

bool _test_enable_sporadic_malloc_failures = false;

static
aresult_t __app_append_unit_string(const char *str)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_STRING(str);

    DIAG("Adding unit '%s'", str);

    _nr_units_to_exec++;

    if (FAILED(ret = TREALLOC((void **)&_units_to_execute, sizeof(char *) * (_nr_units_to_exec + 1)))) {
        DIAG("Error while allocating.");
        goto done;
    }

    if (FAILED(ret = tstrdup((char **)&_units_to_execute[_nr_units_to_exec - 1], str))) {
        DIAG("Cannot strdup...");
        goto done;
    }

    _units_to_execute[_nr_units_to_exec] = NULL;

done:
    return ret;
}

static
aresult_t __app_free_units(void)
{
    aresult_t ret = A_OK;

    for (size_t i = 0; i < _nr_units_to_exec; i++) {
        if (_units_to_execute[i] != NULL) {
            TFREE(_units_to_execute[i]);
        }
    }

    TFREE(_units_to_execute);

    return ret;
}

static
aresult_t _app_list_units(void)
{
    aresult_t ret = A_OK;

    struct test_suite *suite = NULL;

    printf("--- Available Test Suites ---\n");

    CR_FOR_EACH_LOADABLE(suite, _all_test_suites) {
        printf("    %s\n", suite->name);
    }

    return ret;
}

static
aresult_t _set_up_malloc_params(const char *arg)
{
    aresult_t ret = A_OK;

    if (!strcmp(arg, "random")) {
        _test_enable_sporadic_malloc_failures = true;
    } else {
        ret = A_E_INVAL;
        goto done;
    }

done:
    return ret;
}

static
aresult_t _app_usage(const char *app_name)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != app_name);
    TSL_ASSERT_ARG('\0' != *app_name);

    fprintf(stderr, "Usage: %s [-L] [-c config filename] [-M malloc_mode]\n", app_name);
    fprintf(stderr, "    -L                 list available units and exit\n");
    fprintf(stderr, "    -c [config]        specify a configuration file. This can be specified multiple times\n");
    fprintf(stderr, "    -M [mode]          specify a malloc failure mode. Supported modes are:\n");
    fprintf(stderr, "           random      malloc should randomly fail during the run\n");
    fprintf(stderr, "    -V                 return the revision this test was built against\n");
    fprintf(stderr, "\n");

    _app_list_units();

    exit(EXIT_SUCCESS);

    return ret;
}

static
aresult_t _app_handle_args(int argc, char * const argv[], struct config *cfg)
{
    aresult_t ret = A_OK;

    int c = -1;

    while (-1 != (c = getopt(argc, argv, "Lc:M:hV"))) {
        switch (c) {
        case 'L':
            _list_units = true;
            break;
        case 'c':
            TSL_BUG_IF_FAILED(config_add(cfg, optarg));
            break;
        case 'M':
            if (FAILED(ret = _set_up_malloc_params(optarg))) {
                TEST_MSG(SEV_ERROR, "INVALID-MALLOC-FLAG", "Error: Invalid malloc flag: '%s'", optarg);
                goto done;
            }
            break;
        case 'h':
            TSL_BUG_IF_FAILED(_app_usage(argv[0]));
        case 'V':
            printf("%s\n", tsl_get_version());
            exit(EXIT_SUCCESS);
        }
    }

    for (int i = optind; i < argc; i++) {
        TSL_BUG_IF_FAILED(__app_append_unit_string(argv[i]));
    }

done:
    return ret;
}

int main(int argc, char *argv[])
{
    int ret = EXIT_FAILURE;

    struct config *cfg = NULL;

    if (argc > 1) {
        TSL_BUG_IF_FAILED(config_new(&cfg));
        TSL_BUG_IF_FAILED(_app_handle_args(argc, argv, cfg));

        if (true == _list_units) {
            TSL_BUG_IF_FAILED(_app_list_units());
            goto done;
        }
    }

    if (FAILED(app_init(argv[0], cfg))) {
        TEST_MSG(SEV_FATAL, "CANT_START_TSL", "Failed to initialize the Trading Standard Library.");
        goto done;
    }

    TEST_MSG(SEV_INFO, "STARTING_TEST", "Starting to execute test suites.");

    if (FAILED(tf_execute_all_test_suites(_units_to_execute))) {
        TEST_MSG(SEV_INFO, "TESTS_FAILED", "One or more units within a suite failed to execute successfully.");
        goto done;
    }

    TEST_MSG(SEV_INFO, "TESTS_DONE", "All tests have been executed.");

    ret = EXIT_SUCCESS;

done:
    TSL_BUG_IF_FAILED(__app_free_units());
    if (NULL != cfg) {
        TSL_BUG_IF_FAILED(config_delete(&cfg));
    }
    return ret;
}

