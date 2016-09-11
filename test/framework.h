#pragma once

#include <tsl/errors.h>
#include <tsl/sections.h>

#define TEST_PASTE(x, y) x ## y
#define TEST_SUITE_NAME(x) TEST_PASTE(test_suite_tests_, x)
#define TEST_SUITE_NAME_STR(x) ("test_suite_tests_" #x)

/** \file framework.h The TSL Test Framework
 * Include this file to use the test framework for your unit tests
 */

typedef aresult_t (*test_suite_cleanup_func_t)(void);
typedef aresult_t (*test_suite_before_test_func_t)(void);
typedef aresult_t (*test_suite_after_test_func_t)(void);
typedef aresult_t (*test_suite_setup_func_t)(void);

typedef aresult_t (*test_suite_single_test_func_t)(void);

/**
 * Declaration of a single test. A single test must be a member of a named suite.
 */
struct test_suite_unit {
    /**
     * Name of this single test
     */
    const char *name;

    /**
     * Name of the file this test is in
     */
    const char *file;

    /**
     * The suite this unit is associated with
     */
    const char *suite_name;

    /**
     * The actual test function
     */
    test_suite_single_test_func_t test;
};

/**
 * Macro to declare a single unit test. Use this like you'd declare a function.
 */
#define TEST_DECLARE_UNIT(_name, _suite) \
    static aresult_t test_unit_ ## _suite ## _ ## _name(void); \
    static struct test_suite_unit __test_unit_ ## _suite ## _ ## _name = { \
        .name = #_name, \
        .file = __FILE__, \
        .suite_name = TEST_SUITE_NAME_STR(_suite), \
        .test = test_unit_ ## _suite ## _ ## _name, \
    }; \
    CR_LOADABLE(test_suite_tests_ ## _suite, __test_unit_ ## _suite ## _ ## _name); \
    static aresult_t test_unit_ ## _suite ## _ ## _name(void)


/**
 * Declaration of a test suite. A test suite is an aggregate of multiple tests that
 * must succeed in order for the suite to be declared successful. Each test should be
 * a standalone unit (i.e. no ordering dependencies).
 *
 * Typically you don't interact with this structure directly, unless you're manipulating
 * the test framework directly.
 */
struct test_suite {
    /**
     * The name of this test suite
     */
    const char *name;

    /**
     * The name of the linker section this suite lives in
     */
    const char *sec_name;

    /**
     * The start of the test units
     */
    struct test_suite_unit **start_units;

    /**
     * The end of the test units
     */
    struct test_suite_unit **end_units;

    /**
     * The cleanup function for this suite
     */
    test_suite_cleanup_func_t cleanup;

    /**
     * The specialized setup function for this suite
     */
    test_suite_setup_func_t setup;

    /**
     * A specialized function to be run before every test in the suite
     */
    test_suite_before_test_func_t before;

    /**
     * A specialized function to be run after every test in the suite
     */
    test_suite_after_test_func_t after;
};

/**
 * Declare a new test suite
 */
#define TEST_DECLARE_SUITE(_name, _cleanup, _setup, _before, _after) \
    extern struct test_suite_unit * __start_test_suite_tests_ ## _name []; \
    extern struct test_suite_unit * __stop_test_suite_tests_ ## _name []; \
    static struct test_suite __test_suite_ ## _name = { \
        .name = #_name, \
        .sec_name = TEST_SUITE_NAME_STR(_name), \
        .cleanup = (_cleanup), \
        .setup = (_setup), \
        .before = (_before), \
        .after = (_after), \
        .start_units = __start_test_suite_tests_ ## _name, \
        .end_units = __stop_test_suite_tests_ ## _name, \
    }; \
    CR_LOADABLE(_all_test_suites, __test_suite_ ## _name)

#define TEST_LOG_LEVEL_DEBUG          0
#define TEST_LOG_LEVEL_INFO           1
#define TEST_LOG_LEVEL_WARN           2
#define TEST_LOG_LEVEL_ERROR          3

void tf_print_log(int level, char *file, int line, char *fmt, ...);

#define TEST_DBG(_fmt, ...) do { tf_print_log(TEST_LOG_LEVEL_DEBUG, __FILE__, __LINE__, _fmt, ##__VA_ARGS__); } while (0)
#define TEST_INF(_fmt, ...) do { tf_print_log(TEST_LOG_LEVEL_INFO, __FILE__, __LINE__, _fmt, ##__VA_ARGS__); } while (0)
#define TEST_WRN(_fmt, ...) do { tf_print_log(TEST_LOG_LEVEL_WARN, __FILE__, __LINE__, _fmt, ##__VA_ARGS__); } while (0)
#define TEST_ERR(_fmt, ...) do { tf_print_log(TEST_LOG_LEVEL_ERROR, __FILE__, __LINE__, _fmt, ##__VA_ARGS__); } while (0)
