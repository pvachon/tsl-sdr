#include <pager/pager_pocsag.h>

#include <test/assert.h>
#include <test/framework.h>

#include <tsl/safe_string.h>
#include <tsl/safe_alloc.h>
#include <tsl/assert.h>

#include <stdlib.h>
#include <stdio.h>

static
const int16_t *samples = NULL;

static
size_t nr_samples = 0;

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

    TSL_BUG_IF_FAILED(tasprintf(&file_path, "%s/%s", test_data_dir, "pocsag_single_burst_38400.raw"));

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
        TEST_ERR("Make sure PAGER_TEST_DATA_DIR is set to point to where we can find pocsag_single_burst_25khz.raw");
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


static
aresult_t _test_pocsag_on_message_simple_cb(
        struct pager_pocsag *pocsag,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len)
{
    return A_OK;
}

static
aresult_t _test_pocsag_on_num_message_simple_cb(
        struct pager_pocsag *pocsag,
        uint16_t baud_rate,
        uint32_t capcode,
        const char *data,
        size_t data_len)
{
    return A_OK;
}

TEST_DECLARE_UNIT(test_one_shot, pocsag)
{
    struct pager_pocsag *pocsag = NULL;

    TEST_ASSERT_OK(pager_pocsag_new(&pocsag, 929612500ul, _test_pocsag_on_num_message_simple_cb, _test_pocsag_on_message_simple_cb));
    TEST_ASSERT_OK(pager_pocsag_on_pcm(pocsag, samples, nr_samples));
    TEST_ASSERT_OK(pager_pocsag_delete(&pocsag));

    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, pocsag)
{
    struct pager_pocsag *pocsag = NULL;

    TEST_ASSERT_OK(pager_pocsag_new(&pocsag, 929612500ul, _test_pocsag_on_num_message_simple_cb, _test_pocsag_on_message_simple_cb));
    TEST_ASSERT_NOT_NULL(pocsag);
    TEST_ASSERT_OK(pager_pocsag_delete(&pocsag));
    TEST_ASSERT_EQUALS(pocsag, NULL);

    return A_OK;
}

TEST_DECLARE_SUITE(pocsag, test_pager_pocsag_cleanup, test_pager_pocsag_setup, NULL, NULL);

