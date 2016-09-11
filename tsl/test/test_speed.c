#include <tsl/test/helpers.h>
#include <tsl/safe_alloc.h>
#include <tsl/speed.h>

#include <stdlib.h>
#include <string.h>
#include <malloc.h>

TEST_DECL(test_speed)
{
    char *bufptr = memalign(64, 128);
    char *outbuf = memalign(64, 128);

    TEST_ASSERT_NOT_EQUALS(bufptr, NULL);
    TEST_ASSERT_NOT_EQUALS(outbuf, NULL);

    TSL_SSE_PREPARE();

    memset(outbuf, 0, 128);

    for (int i = 0; i < 128; ++i) {
        bufptr[i] = 127 - i;
    }

    TSL_LOAD_ALIGNED_128(bufptr, xmm0);
    TSL_STORE_ALIGNED_128(outbuf, xmm0);

    for (int i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUALS(outbuf[i], 127-i);
    }

    /* Simple test for the prefetch instructions */
    TSL_PREFETCH_ALL_LEVELS(&outbuf[16]);
    TSL_PREFETCH_L2_HIGHER(&outbuf[17]);
    TSL_PREFETCH_WRITE(outbuf);

    TEST_ASSERT_EQUALS(outbuf[16], 0);
    TEST_ASSERT_EQUALS(outbuf[17], 0);

    TFREE(bufptr);
    return TEST_OK;
}
