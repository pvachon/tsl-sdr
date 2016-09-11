
#include <config/engine.h>

#include <test/assert.h>
#include <test/framework.h>
#include <tsl/basic.h>
#include <tsl/panic.h>
#include <tsl/safe_alloc.h>

#include <string.h>

static
aresult_t engine_cleanup(void)
{
    return A_OK;
}

static
aresult_t engine_setup(void)
{
    return A_OK;
}

TEST_DECLARE_UNIT(test_basics, engine)
{
    const char *test_file = "config/test/config.json";
    struct config *cfg = NULL;
    const char *path = NULL;
    int cpucore = -1;

    TEST_ASSERT_EQUALS(config_new(&cfg), A_OK);
    TEST_ASSERT_NOT_EQUALS(NULL, cfg);

    TEST_ASSERT_EQUALS(config_add(cfg, test_file), A_OK);

    TEST_ASSERT_EQUALS(config_get_string(cfg, &path, "cmdIf.transport"), A_OK);
    TEST_ASSERT_NOT_EQUALS(path, NULL);
    TEST_ASSERT_EQUALS(strcmp("udp", path), 0);
    TEST_ASSERT_EQUALS(config_get_integer(cfg, &cpucore, "cmdIf.cpuCore"), A_OK);
    TEST_ASSERT_EQUALS(cpucore, 1);

    TEST_ASSERT_OK(config_delete(&cfg));
    TEST_ASSERT_EQUALS(cfg, NULL);

    return A_OK;
}

static
const char _iterators_test_config[] = "{\n"
    "  \"testInt\":[0,1,2,3,4,5],\n"
    "  \"testStr\":[\"foo\", \"bar\", \"baz\"]\n"
    "}\n";

static
const char *expected_str[] = {
    "foo",
    "bar",
    "baz",
};

TEST_DECLARE_UNIT(test_iterators, engine)
{
    aresult_t ret = A_OK;

    struct config *cfg = NULL;
    struct config test_int = CONFIG_INIT_EMPTY,
                  test_str = CONFIG_INIT_EMPTY,
                  test_tmp = CONFIG_INIT_EMPTY;
    int val = -1;
    const char *str = NULL;
    size_t ctr = 0;

    TEST_ASSERT_OK(config_new(&cfg));
    TEST_ASSERT_NOT_EQUALS(NULL, cfg);

    TEST_ASSERT_OK(config_add_string(cfg, _iterators_test_config));
    TEST_ASSERT_OK(config_get(cfg, &test_int, "testInt"));
    TEST_ASSERT_OK(config_get(cfg, &test_str, "testStr"));

    /* 1. Test the Integer Array iteration case */
    CONFIG_ARRAY_FOR_EACH(val, &test_int, ret, ctr) {
        TEST_ASSERT_EQUALS(val, (int)ctr);
    }
    TEST_ASSERT_OK(ret);
    TEST_ASSERT_EQUALS(ctr, 6);

    /* 2. Test the String Array iteration case */
    CONFIG_ARRAY_FOR_EACH(str, &test_str, ret, ctr) {
        TEST_ASSERT_EQUALS(str, expected_str[ctr]);
    }
    TEST_ASSERT_OK(ret);
    TEST_ASSERT_EQUALS(ctr, 3);

    /* 3. Make sure we fail when trying to retrieve strings as an integer */
    CONFIG_ARRAY_FOR_EACH(val, &test_str, ret, ctr) {
        TEST_ASSERT_NOT_EQUALS(true, false);
    }
    TEST_ASSERT_FAILED(ret);
    TEST_ASSERT_EQUALS(ctr, 0);

    /* Finally, try retrieving strings as a generic config object and inspect */
    CONFIG_ARRAY_FOR_EACH(test_tmp, &test_str, ret, ctr) {
        TEST_ASSERT_EQUALS(test_tmp.atom_type, CONFIG_ATOM_STRING);
        TEST_ASSERT_EQUALS(test_tmp.atom_string, expected_str[ctr]);
    }
    TEST_ASSERT_OK(ret);
    TEST_ASSERT_EQUALS(ctr, 3);

    TEST_ASSERT_OK(config_delete(&cfg));
    TEST_ASSERT_EQUALS(NULL, cfg);

    return A_OK;
}

TEST_DECLARE_SUITE(engine, engine_cleanup, engine_setup, NULL, NULL);
