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
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        bool fragmented,
        bool maildrop,
        uint8_t seq_num,
        const char *message_bytes,
        size_t message_len)
{
    return A_OK;
}

static
aresult_t _test_flex_on_num_message_simple_cb(
        struct pager_flex *flex,
        uint16_t baud,
        uint8_t phase,
        uint8_t cycle_no,
        uint8_t frame_no,
        uint64_t cap_code,
        const char *message_bytes,
        size_t message_len)
{
    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, flex)
{
    struct pager_flex *flex = NULL;

    TEST_ASSERT_OK(pager_flex_new(&flex, 929612500ul, _test_flex_on_message_simple_cb, _test_flex_on_num_message_simple_cb, NULL));
    TEST_ASSERT_OK(pager_flex_delete(&flex));

    return A_OK;
}

TEST_DECLARE_SUITE(flex, test_pager_flex_cleanup, test_pager_flex_setup, NULL, NULL);

