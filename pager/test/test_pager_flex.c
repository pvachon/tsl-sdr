#include <pager/pager.h>

#include <test/assert.h>
#include <test/framework.h>

static
aresult_t test_pager_flex_setup(void)
{
    return A_OK;
}

static
aresult_t test_pager_flex_cleanup(void)
{
    return A_OK;
}

static
aresult_t _test_flex_on_message_simple_cb(
        struct pager_flex *flex,
        uint16_t baud,
        char phase,
        uint32_t cap_code,
        enum pager_flex_msg_type message_type,
        const char *message_bytes,
        size_t message_len)
{
    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, flex)
{
    struct pager_flex *flex = NULL;

    TEST_ASSERT_OK(pager_flex_new(&flex, 929612500ul, _test_flex_on_message_simple_cb));
    TEST_ASSERT_OK(pager_flex_delete(&flex));

    return A_OK;
}

TEST_DECLARE_SUITE(flex, test_pager_flex_cleanup, test_pager_flex_setup, NULL, NULL);

