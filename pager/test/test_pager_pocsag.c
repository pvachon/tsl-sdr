#include <pager/pager_pocsag.h>

#include <test/assert.h>
#include <test/framework.h>

#include <tsl/safe_string.h>
#include <tsl/safe_alloc.h>
#include <tsl/assert.h>
#include <tsl/hexdump.h>

#include <stdlib.h>
#include <stdio.h>

static
const int16_t *samples = NULL;

static
size_t nr_samples = 0;

//#define TEST_FILE_NAME "pocsag_hospital_38400_long.raw"
#define TEST_FILE_NAME "pocsag_38400_test_512bps_hackrf.raw"

static
aresult_t test_pager_pocsag_setup(void)
{
    aresult_t ret = A_OK;

    const char *test_data_dir = ".",
               *env_dir = NULL;
    char *file_path = NULL;
    size_t file_len = 0;

    FILE *fp = NULL;

    if (NULL != (env_dir = getenv("PAGER_TEST_DATA_DIR"))) {
        test_data_dir = env_dir;
    }

    TEST_INF("Retrieving POCSAG test data from directory '%s'", test_data_dir);

    TSL_BUG_IF_FAILED(tasprintf(&file_path, "%s/%s", test_data_dir, TEST_FILE_NAME));

    if (NULL == (fp = fopen(file_path, "r"))) {
        TEST_ERR("Failed to open file %s, aborting.", file_path);
        ret = A_E_INVAL;
        goto done;
    }

    fseek(fp, 0, SEEK_END);
    file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (FAILED(ret = TCALLOC((void **)&samples, 1, file_len))) {
        goto done;
    }

    if (1 != fread((int16_t *)samples, file_len, 1, fp)) {
        TEST_ERR("Failed to read %zu bytes from file %s, aborting.", file_len, file_path);
        ret = A_E_INVAL;
        goto done;
    }

    nr_samples = file_len / sizeof(int16_t);

done:
    if (FAILED(ret)) {
        TEST_ERR("Make sure PAGER_TEST_DATA_DIR is set to point to where we can find " TEST_FILE_NAME);
        if (NULL != samples) {
            TFREE(samples);
        }
    }

    if (NULL != fp) {
        fclose(fp);
        fp = NULL;
    }

    if (NULL != file_path) {
        TFREE(file_path);
    }

    return ret;
}

static
aresult_t test_pager_pocsag_cleanup(void)
{
    aresult_t ret = A_OK;

    if (NULL != samples) {
        TFREE(samples);
    }

    nr_samples = 0;

    return ret;
}

static const
uint32_t _unpack_testi_7f[] = {
    0x1FFFFE,
    0x1FFFFE,
    0x1FFFFE,
    0x1FFFFE,

    0x1FFFFE,
    0x1FFFFE,
    0x1FFFFE,
    0x1FFFFE,

    0x1FFFFE,
    0x1FFFFE,
    0x1FFFFE,
    0x1FFFFE,

    0x1FFFFE,
    0x1FFFFE,
};

static
aresult_t _pager_pocsag_unpack_batch_ascii(const uint32_t *batch_words, size_t batch_nr_words,
        uint8_t *val, size_t max_val, size_t *pnr_val)
{
    aresult_t ret = A_OK;

    size_t byte_off = 0;
    size_t nr_bits = 0;
    uint32_t acc = 0;

    for (size_t i = 0; i < batch_nr_words; i++) {
        acc |= ((batch_words[i] >> 1) & 0xfffffu) << nr_bits;
        nr_bits += 20;
        while (nr_bits >= 7) {
            val[byte_off++] = acc & 0x7f;
            if (max_val <= byte_off) {
                ret = A_E_INVAL;
                goto done;
            }
            nr_bits -= 7;
        }
    }

    TEST_INF("There are %zu bits left", nr_bits);

    *pnr_val = byte_off;

done:
    return ret;
}

TEST_DECLARE_UNIT(test_7b_unpack, pocsag)
{
    uint8_t unpacked[512];
    size_t nr_unpacked;

    TEST_ASSERT_OK(_pager_pocsag_unpack_batch_ascii(_unpack_testi_7f, sizeof(_unpack_testi_7f)/sizeof(uint32_t),
                unpacked, sizeof(unpacked), &nr_unpacked));
    TEST_ASSERT_EQUALS(nr_unpacked, 40);
    for (size_t i = 0; i < nr_unpacked; i++) {
        TEST_ASSERT_EQUALS(unpacked[i], 0x7f);
    }

    return A_OK;
}

static
aresult_t _test_pocsag_on_message_simple_cb(
        struct pager_pocsag *pocsag,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len,
        uint8_t function)
{
    fprintf(stderr, "POCSAG%u: ALN(%u): [%8u]: %s\n", (unsigned)baud_rate, (unsigned)function, capcode, data);
    hexdump_dump_hex(data, data_len);
    return A_OK;
}

static
aresult_t _test_pocsag_on_num_message_simple_cb(
        struct pager_pocsag *pocsag,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len,
        uint8_t function)
{
    fprintf(stderr, "POCSAG%u: NUM(%u): [%8u]: %s\n", (unsigned)baud_rate, function, capcode, data);
    return A_OK;
}

TEST_DECLARE_UNIT(test_one_shot, pocsag)
{
    struct pager_pocsag *pocsag = NULL;

    TEST_ASSERT_OK(pager_pocsag_new(&pocsag, 929612500ul, _test_pocsag_on_num_message_simple_cb, _test_pocsag_on_message_simple_cb, false));
    TEST_ASSERT_OK(pager_pocsag_on_pcm(pocsag, samples, nr_samples));
    TEST_ASSERT_OK(pager_pocsag_delete(&pocsag));

    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, pocsag)
{
    struct pager_pocsag *pocsag = NULL;

    TEST_ASSERT_OK(pager_pocsag_new(&pocsag, 929612500ul, _test_pocsag_on_num_message_simple_cb, _test_pocsag_on_message_simple_cb, false));
    TEST_ASSERT_NOT_NULL(pocsag);
    TEST_ASSERT_OK(pager_pocsag_delete(&pocsag));
    TEST_ASSERT_EQUALS(pocsag, NULL);

    return A_OK;
}

TEST_DECLARE_SUITE(pocsag, test_pager_pocsag_cleanup, test_pager_pocsag_setup, NULL, NULL);

