#include <tsl/test/helpers.h>
#include <tsl/panic.h>
#include <app/app.h>
#include <tsl/basic.h>

#include <stdlib.h>
#include <stdio.h>

TEST_DECL(test_basic) {
    TEST_ASSERT_EQUALS( BL_MIN3(1, 2, 3), 1 );
    TEST_ASSERT_EQUALS( BL_MIN3(2, 1, 3), 1 );
    TEST_ASSERT_EQUALS( BL_MIN3(3, 2, 1), 1 );
    TEST_ASSERT_EQUALS( BL_MIN3(3, 3, 1), 1 );
    TEST_ASSERT_EQUALS( BL_MIN3(1, 3, 3), 1 );
    return TEST_OK;
}

int main(int argc, char *argv[])
{
    TEST_START(tsl);
#ifdef __x86_64__
    TEST_CASE(test_coro);
#endif
    TEST_CASE(test_basic);
    TEST_CASE(test_format_sockaddr_t_null);
    TEST_CASE(test_format_sockaddr_t_ipv4);
    TEST_CASE(test_format_sockaddr_t_ipv6);
    TEST_CASE(test_alloc_basic);
    TEST_CASE(test_refcnt_basic);
    TEST_CASE(test_rbtree_lifecycle);
    TEST_CASE(test_rbtree_corner_cases);
    TEST_CASE(test_cpu_mask);
#ifdef __x86_64__
    TEST_CASE(test_speed);
#endif
    TEST_FINISH(tsl);
    return EXIT_SUCCESS;
}
