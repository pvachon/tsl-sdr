#include <tsl/test/helpers.h>
#include <tsl/refcnt.h>
#include <tsl/basic.h>

#define TEST_MAGIC  0xbebafeca

struct test_refcnt {
    uint32_t magic;
    unsigned int id;
    struct refcnt rc;
};

/* Stupid mechanism to get a value out of the destructor */
static
int test_failed = 1;

static
void test_refcnt_destructor(struct refcnt *rc)
{
    struct test_refcnt *tr = BL_CONTAINER_OF(rc, struct test_refcnt, rc);

    if (tr->id != 42 || tr->magic != TEST_MAGIC) {
        test_failed = 1;
    }

    test_failed = 0;
}

TEST_DECL(test_refcnt_basic)
{
    struct test_refcnt first;

    first.magic = TEST_MAGIC;
    first.id = 42;

    /* Test 1: Initialize reference counting */
    TEST_ASSERT_EQUALS(refcnt_init(&first.rc, test_refcnt_destructor), A_OK);

    TEST_ASSERT(first.rc.refcnt == 1);

    /* Test 2: Check that refcnt_check does the right thing for a valid object */
    int valid = 0;
    TEST_ASSERT_EQUALS(refcnt_check(&first.rc, &valid), A_OK);
    TEST_ASSERT_NOT_EQUALS(valid, 0);

    /* Test 3: Get a new reference */
    TEST_ASSERT_EQUALS(refcnt_get(&first.rc), A_OK);
    TEST_ASSERT(first.rc.refcnt == 2);

    /* Test 4: Release a reference */
    TEST_ASSERT_EQUALS(refcnt_release(&first.rc), A_OK);
    TEST_ASSERT(first.rc.refcnt == 1);

    /* Test 5: Release final reference */
    TEST_ASSERT_EQUALS(refcnt_release(&first.rc), A_OK);
    TEST_ASSERT(first.rc.refcnt == 0);
    TEST_ASSERT(test_failed == 0);

    /* Test 6: Try to release again */
    TEST_ASSERT(AFAILED(refcnt_release(&first.rc)));
    TEST_ASSERT(first.rc.refcnt == 0);

    return TEST_OK;
}
