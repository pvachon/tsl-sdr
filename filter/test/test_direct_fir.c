#include <filter/filter.h>

#include <test/assert.h>
#include <test/framework.h>

static
aresult_t test_direct_fir_setup(void)
{
    return A_OK;
}

static
aresult_t test_direct_fir_cleanup(void)
{
    return A_OK;
}


TEST_DECLARE_UNIT(test_smoke, flex)
{
    return A_OK;
}

TEST_DECLARE_SUITE(flex, test_direct_fir_cleanup, test_direct_fir_setup, NULL, NULL);

