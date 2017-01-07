#include <filter/filter.h>

#include <test/assert.h>
#include <test/framework.h>

static const
int16_t test_polyphase_fir_coeffs[] = {
    255, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
};

static
aresult_t test_polyphase_fir_setup(void)
{
    return A_OK;
}

static
aresult_t test_polyphase_fir_cleanup(void)
{
    return A_OK;
}


TEST_DECLARE_UNIT(test_smoke, polyphase)
{
    struct polyphase_fir *pfir = NULL;

    TEST_ASSERT_OK(polyphase_fir_new(&pfir, sizeof(test_polyphase_fir_coeffs)/sizeof(int16_t),
                test_polyphase_fir_coeffs, 3, 2));
    TEST_ASSERT_OK(polyphase_fir_delete(&pfir));
    return A_OK;
}

TEST_DECLARE_SUITE(polyphase, test_polyphase_fir_cleanup, test_polyphase_fir_setup, NULL, NULL);

