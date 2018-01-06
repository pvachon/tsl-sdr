#include <ais/ais_demod.h>

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

//#define TEST_FILE_NAME "ais_48khz_16b_raw.bin"
#define TEST_FILE_NAME "ais_single_sample.bin"

static
aresult_t test_ais_demod_setup(void)
{
    aresult_t ret = A_OK;

    const char *test_data_dir = ".",
               *env_dir = NULL;
    char *file_path = NULL;
    size_t file_len = 0;

    FILE *fp = NULL;

    if (NULL != (env_dir = getenv("AIS_TEST_DATA_DIR"))) {
        test_data_dir = env_dir;
    }

    TEST_INF("Retrieving AIS test data from directory '%s'", test_data_dir);

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
        TEST_ERR("Make sure AIS_TEST_DATA_DIR is set to point to where we can find " TEST_FILE_NAME);
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
aresult_t test_ais_demod_cleanup(void)
{
    aresult_t ret = A_OK;

    if (NULL != samples) {
        TFREE(samples);
    }

    nr_samples = 0;

    return ret;
}

static
aresult_t _test_on_message_cb(struct ais_demod *demod, const uint8_t *packet, bool fcs_valid)
{
    return A_OK;
}

TEST_DECLARE_UNIT(test_one_shot, ais_demod)
{
    struct ais_demod *demod = NULL;

    TEST_INF("Processing %zu samples in one shot.", nr_samples);

    TEST_ASSERT_OK(ais_demod_new(&demod, _test_on_message_cb, 162025000ul));
    TEST_ASSERT_NOT_NULL(demod);
    TEST_ASSERT_OK(ais_demod_on_pcm(demod, samples, nr_samples));
    TEST_ASSERT_OK(ais_demod_delete(&demod));

    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, ais_demod)
{
    struct ais_demod *demod = NULL;

    TEST_ASSERT_OK(ais_demod_new(&demod, _test_on_message_cb, 162025000ul));
    TEST_ASSERT_NOT_NULL(demod);
    TEST_ASSERT_OK(ais_demod_delete(&demod));

    return A_OK;
}

TEST_DECLARE_SUITE(ais_demod, test_ais_demod_cleanup, test_ais_demod_setup, NULL, NULL);

