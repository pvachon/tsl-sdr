#include <tsl/test/helpers.h>
#include <tsl/cpumask.h>

TEST_DECL(test_cpu_mask)
{
    struct cpu_mask *msk = NULL;

    TEST_ASSERT_EQUALS(cpu_mask_new(&msk), A_OK);
    TEST_ASSERT_NOT_EQUALS(msk, NULL);

    TEST_ASSERT_EQUALS(cpu_mask_set(msk, 1), A_OK);
    int val = 0;
    TEST_ASSERT_EQUALS(cpu_mask_test(msk, 1, &val), A_OK);
    TEST_ASSERT(val == 1);

    TEST_ASSERT_EQUALS(cpu_mask_clear(msk, 1), A_OK);
    TEST_ASSERT_EQUALS(cpu_mask_test(msk, 1, &val), A_OK);
    TEST_ASSERT(val == 0);

    TEST_ASSERT_EQUALS(cpu_mask_set(msk, 2), A_OK);
    TEST_ASSERT_EQUALS(cpu_mask_test(msk, 2, &val), A_OK);
    TEST_ASSERT(val == 1);

    TEST_ASSERT_EQUALS(cpu_mask_clear_all(msk), A_OK);
    TEST_ASSERT_EQUALS(cpu_mask_test(msk, 2, &val), A_OK);
    TEST_ASSERT(val == 0);

    TEST_ASSERT_EQUALS(cpu_mask_delete(&msk), A_OK);
    TEST_ASSERT_EQUALS(msk, NULL);

    return TEST_OK;
}
