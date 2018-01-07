#include <ais/ais_demod.h>

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

#define TEST_FILE_NAME "ais_48khz_16b_raw.bin"
//#define TEST_FILE_NAME "ais_single_sample.bin"

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

    TEST_INF("Attempting to open AIS test data from file '%s'", file_path);

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
char _test_to_ascii_armor(uint8_t in)
{
    if (in <= 39) {
        return in + 48;
    } else {
        return in - 40 + 96;
    }
}

static
aresult_t _test_on_message_cb(struct ais_demod *demod, const uint8_t *packet, bool fcs_valid)
{
    uint8_t msg_id = 0,
            offs = 0;
    uint32_t mmsi = 0;
    uint8_t msg_ascii_6[168/6];

    memset(msg_ascii_6, 0, sizeof(msg_ascii_6));

    for (size_t i = 0; i < sizeof(msg_ascii_6); i += 4) {
        uint32_t accum = 0;
        for (size_t j = offs; j < offs + 3; j++) {
            accum <<= 8;
            accum |= packet[j];
        }
        offs += 3;
        for (size_t j = 0; j < 4; j++) {
            msg_ascii_6[i + j] = (accum >> ((3 - j) * 6)) & 0x3f;
        }
    }

    printf("Ascii Armored version: ");
    for (size_t i = 0; i < sizeof(msg_ascii_6); i++) {
        printf("%c", _test_to_ascii_armor(msg_ascii_6[i]));
    }
    printf("\n");

    msg_id = msg_ascii_6[0];

    /* MMSI is 32 bits long */
    for (size_t i = 0; i < 5; i++) {
        mmsi <<= 6;
        mmsi |= msg_ascii_6[1 + i] & 0x3f;
    }

    mmsi <<= 2;
    mmsi |= (msg_ascii_6[6] >> 4) & 0x3;

    printf("MsgId: %02u MMSI: %8u\n", msg_id, mmsi);

    hexdump_dump_hex(msg_ascii_6, sizeof(msg_ascii_6));
    hexdump_dump_hex(packet, 168/8);
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

