#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <tsl/errors.h>

#define TEST_DECL(x) int x(void); int x(void)

#define TEST_OK                 0
#define TEST_FAILED             1

#define TEST_ASSERT(x) \
    do {                            \
        if (!(x)) {                 \
            fprintf(stderr, "%s:%d - Assertion failure: " #x " == FALSE\n", __FILE__, __LINE__); \
            return TEST_FAILED;     \
        }                           \
    } while (0)

#define TEST_ASSERT_EQUALS(x, y) \
    do {                            \
        if (!((x) == (y))) {        \
            fprintf(stderr, "%s:%d - Equality assertion failed: " #x " != " #y "\n", __FILE__, __LINE__); \
            return TEST_FAILED;     \
        }                           \
    } while (0)

#define TEST_ASSERT_NOT_EQUALS(x, y) \
    do {                            \
        if (!((x) != (y))) {        \
            fprintf(stderr, "%s:%d - Equality assertion failed: " #x " == " #y "\n", __FILE__, __LINE__); \
            return TEST_FAILED;     \
        }                           \
    } while (0)

#define TEST_ASSERT_OK(x)           \
    do {                            \
        if (!(x == A_OK)) {         \
            fprintf(stderr, "%s:%d - Assertion failure: " #x " != A_OK\n", __FILE__, __LINE__); \
            return TEST_FAILED;     \
        }                           \
    } while (0)

#define TEST_START(name) \
    unsigned int __failures = 0; \
    fprintf(stderr, "Starting test suite " #name "\n");


#define TEST_CASE(func) \
    do {                                            \
        extern int func(void);                      \
        fprintf(stderr, "Running test " #func "\n"); \
        if ((func()) != 0) {                        \
            fprintf(stderr, "Failure in " #func " (#%d)\n", __COUNTER__); \
            __failures++;                           \
        }                                           \
        fflush(stderr);                             \
    } while (0)

#define TEST_FINISH(name) \
    do {                                            \
        fprintf(stderr, "Test complete. %u failures occurred in %u tests.\n", __failures, __COUNTER__); \
        exit(__failures != 0 ? EXIT_FAILURE : EXIT_SUCCESS); \
    } while (0)
