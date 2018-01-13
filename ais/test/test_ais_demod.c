#include <ais/ais_demod.h>
#include <ais/ais_decode.h>

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
uint32_t _ais_decode_get_bitfield(const uint8_t *packet, size_t packet_len,
        size_t offset, size_t len)
{
    uint64_t acc = 0;
    size_t nr_bytes = (len + 7)/8,
           start_byte = offset/8;

    for (size_t i = 0; i < nr_bytes; i++) {
        acc <<= 8;
        acc |= packet[i + start_byte];
    }

    size_t end_bit = (offset + len) % 8;

    acc >>= 8 - end_bit;
    acc &= ((1ul << len) - 1);

    return (uint32_t)acc;
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
aresult_t _test_on_message_cb(struct ais_demod *demod, void *state, const uint8_t *packet, size_t packet_len, bool fcs_valid)
{
    uint8_t msg_id = 0,
            repeat = 0;
    uint32_t mmsi = 0;
    uint8_t offs = 0;
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

    /* Extract the message type */
    msg_id = (packet[0] >> 2) & 0x3f;

    /* Extract repeat indicator */
    repeat = packet[0] & 0x3;

    /* Extract the MMSI from the packet */
    mmsi  = (uint32_t)packet[1] << 22;
    mmsi |= (uint32_t)packet[2] << 14;
    mmsi |= (uint32_t)packet[3] << 6;
    mmsi |= ((uint32_t)packet[4] >> 2) & 0x3f;

    uint32_t mmsi_test = _ais_decode_get_bitfield(packet, packet_len, 8, 30);

    TEST_INF("MsgId: %02u Rpt: %1u MMSI: %9u Test MMSI: %9u (Len: %zu bytes)", msg_id, repeat, mmsi, mmsi_test, packet_len);

    return A_OK;
}

TEST_DECLARE_UNIT(test_one_shot_decoder, ais_demod)
{
    struct ais_decode *decoder = NULL;

    TEST_INF("Processing %zu samples in one shot.", nr_samples);

    TEST_ASSERT_OK(ais_decode_new(&decoder, 162025000ul, NULL, NULL, NULL));
    TEST_ASSERT_NOT_NULL(decoder);
    TEST_ASSERT_OK(ais_decode_on_pcm(decoder, samples, nr_samples));
    TEST_ASSERT_OK(ais_decode_delete(&decoder));

    return A_OK;
}

TEST_DECLARE_UNIT(test_one_shot, ais_demod)
{
    struct ais_demod *demod = NULL;

    TEST_INF("Processing %zu samples in one shot.", nr_samples);

    TEST_ASSERT_OK(ais_demod_new(&demod, NULL, _test_on_message_cb, 162025000ul));
    TEST_ASSERT_NOT_NULL(demod);
    TEST_ASSERT_OK(ais_demod_on_pcm(demod, samples, nr_samples));
    TEST_ASSERT_OK(ais_demod_delete(&demod));

    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, ais_demod)
{
    struct ais_demod *demod = NULL;

    TEST_ASSERT_OK(ais_demod_new(&demod, NULL, _test_on_message_cb, 162025000ul));
    TEST_ASSERT_NOT_NULL(demod);
    TEST_ASSERT_OK(ais_demod_delete(&demod));

    return A_OK;
}

TEST_DECLARE_SUITE(ais_demod, test_ais_demod_cleanup, test_ais_demod_setup, NULL, NULL);

