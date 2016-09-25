#pragma once

#include <test/framework.h>
#include <test/filesystem.h>

#include <tsl/errors.h>

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define CHOOSE_IF(_cmp, _istrue, _isfalse) (__builtin_choose_expr(_cmp, _istrue, _isfalse))

void test_print_char(char x);
void test_print_int(long long int x);
void test_print_unsigned(unsigned long long x);
void test_print_pointer(const void *ptr);
void test_print_aresult(aresult_t ret);
void test_print_string(const char *str);
void test_print_boolean(bool val);

#define __TEST_GET_TYPE_NAME_OF(x) _Generic((x), char               : "char",                   \
                                                 short              : "short int",              \
                                                 int                : "int",                    \
                                                 long               : "long int",               \
                                                 long long          : "long long int",          \
                                                 unsigned char      : "unsigned char",          \
                                                 unsigned short     : "unsigned short int",     \
                                                 unsigned int       : "unsigned int",           \
                                                 unsigned long      : "unsigned long int",      \
                                                 unsigned long long : "unsigned long long int", \
                                                 char *             : "string",                 \
                                                 const char *       : "const string",           \
                                                 unsigned char *    : "unsigned string",        \
                                                 default            : "unknown")

#define __TEST_TYPE_IS_STRING(x) _Generic((x), char *       : true,           \
                                               const char * : true,           \
                                               default      : false)

/* Danger - this macro can have side effects. */
#define __TEST_CHECK_EQUALS(__x, __y) \
    ({ \
        CHOOSE_IF(__TEST_TYPE_IS_STRING(__x) && __TEST_TYPE_IS_STRING(__y), \
            !strcmp((const char *)(intptr_t)(__x), (const char *)(intptr_t)(__y)), \
            ((__x) == (__y))); \
    })

/* Danger - this macro can have side effects. */
#define __TEST_CHECK_GREATER_THAN(x, y) \
    ({ \
        __typeof__(x) _x_val = (x); \
        __typeof__(y) _y_val = (y); \
        _x_val > _y_val; \
    })

/* Danger - this macro can have side effects. */
#define __TEST_CHECK_LESS_THAN(x, y) \
    ({ \
        __typeof__(x) _x_val = (x); \
        __typeof__(y) _y_val = (y); \
        _x_val < _y_val; \
    })

#define TEST_PRINT_VALUE_BY_TYPE(_val_) \
    do { \
        _Generic((_val_), \
                char:               test_print_char,        \
                short:              test_print_int,         \
                int:                test_print_int,         \
                long:               test_print_int,         \
                long long:          test_print_int,         \
                unsigned char:      test_print_unsigned,    \
                unsigned short:     test_print_unsigned,    \
                unsigned int:       test_print_unsigned,    \
                unsigned long:      test_print_unsigned,    \
                unsigned long long: test_print_unsigned,    \
                char *:             test_print_string,      \
                const char *:       test_print_string,      \
                bool:               test_print_boolean,     \
                default:            test_print_pointer)((_val_)); \
    } while (0)

#define TEST_ASSERT_EQUALS(x, y) \
    do { \
        __typeof__((x)) _x_val = (x); \
        __typeof__((y)) _y_val = (y); \
        if (!__TEST_CHECK_EQUALS(_x_val, _y_val)) { \
            printf("TEST: Assertion Failed: " #x " != " #y " (x = "); \
            TEST_PRINT_VALUE_BY_TYPE(_x_val); \
            printf(", y = "); \
            TEST_PRINT_VALUE_BY_TYPE(_y_val); \
            printf(") (%s:%d)\n", __FILE__, __LINE__); \
            return A_E_INVAL; \
        } \
    } while (0)

#define TEST_ASSERT_NOT_EQUALS(x, y) \
    do { \
        __typeof__((x)) _x_val = (x); \
        __typeof__((y)) _y_val = (y); \
        if (__TEST_CHECK_EQUALS(_x_val, _y_val)) { \
            printf("TEST: Assertion Failed: " #x " == " #y " (x = "); \
            TEST_PRINT_VALUE_BY_TYPE(_x_val); \
            printf(", y = "); \
            TEST_PRINT_VALUE_BY_TYPE(_y_val); \
            printf(") (%s:%d)\n", __FILE__, __LINE__); \
            return A_E_INVAL; \
        } \
    } while (0)

#define TEST_ASSERT_NOT_NULL(_ptr) TEST_ASSERT_NOT_EQUALS((_ptr), NULL)

#define TEST_ASSERT_OK(invoc) \
    do { \
        aresult_t __ret = (invoc); \
        if (FAILED(__ret)) { \
            printf("TEST: Assertion Failed - invocation " #invoc " returned an error: "); \
            test_print_aresult(__ret); \
            printf(" (%s: %d)\n", __FILE__, __LINE__); \
            return A_E_INVAL; \
        } \
    } while (0)

#define TEST_ASSERT_FAILED(invoc) \
    do { \
        aresult_t __ret = (invoc); \
        if (!FAILED(__ret)) { \
            printf("TEST: Assertion Failed - invocation " #invoc " did not return a failure"); \
            printf(" (%s: %d)\n", __FILE__, __LINE__); \
            return A_E_INVAL; \
        } \
    } while (0)

#define TEST_ASSERT_DIR_EXISTS(dir) \
    do {                                                                                            \
        bool is_dir = false,                                                                        \
             exists = false;                                                                        \
        if (FAILED(test_filesystem_check_exists((dir), &exists, NULL, &is_dir))) {                 \
            printf("TEST: Assertion Failed - error checking directory stat.", (dir));               \
            printf(" (%s: %d)\n", __FILE__, __LINE__);                                              \
            return A_E_INVAL;                                                                       \
        }                                                                                           \
        if (false == exists) {                                                                      \
            printf("TEST: Assertion Failed - %s does not exist.", (dir));                           \
            printf(" (%s: %d)\n", __FILE__, __LINE__);                                              \
            return A_E_INVAL;                                                                       \
        }                                                                                           \
        if (false == is_dir) {                                                                      \
            printf("TEST: Assertion Failed - %s is not a directory.", (dir));                       \
            printf(" (%s: %d)\n", __FILE__, __LINE__);                                              \
            return A_E_INVAL;                                                                       \
        }                                                                                           \
    } while (0)

#define TEST_ASSERT_FILE_DOES_NOT_EXIST(dir) \
    do {                                                                                            \
        bool is_dir = false,                                                                        \
             exists = false;                                                                        \
        if (FAILED(test_filesystem_check_exists((dir), &exists, NULL, &is_dir))) {                 \
            printf("TEST: Assertion Failed - error checking directory stat for %s.", (dir));        \
            printf(" (%s: %d)\n", __FILE__, __LINE__);                                              \
            return A_E_INVAL;                                                                       \
        }                                                                                           \
        if (true == exists) {                                                                       \
            printf("TEST: Assertion Failed - %s already exists.", (dir));                           \
            printf(" (%s: %d)\n", __FILE__, __LINE__);                                              \
            return A_E_INVAL;                                                                       \
        }                                                                                           \
    } while (0)

