#include <pager/mueller_muller.h>

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
aresult_t test_mueller_muller_setup(void)
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

    TEST_INF("Retrieving MM test data from directory '%s'", test_data_dir);

    TSL_BUG_IF_FAILED(tasprintf(&file_path, "%s/%s", test_data_dir, "pocsag_hospital_25khz_long.raw"));

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
aresult_t test_mueller_muller_cleanup(void)
{
    aresult_t ret = A_OK;

    if (NULL != samples) {
        TFREE(samples);
    }

    nr_samples = 0;

    return ret;
}

#define TEST_KW                     0.0001
#define TEST_KM                     0.000004
#define TEST_SAMPLES_PER_BIT        (25000.0f/1200.0f)
#define TEST_ERROR_MARGIN           0.05f
#define TEST_MAX_NR_DECISIONS       (1 << 8)

#define POCSAG_SYNC_CODEWORD        0x7cd215d8ul

TEST_DECLARE_UNIT(test_sample_process, mueller_muller)
{
    int16_t decisions[2 * TEST_MAX_NR_DECISIONS];
    size_t nr_decisions = 0,
           total_decisions = 0,
           samples_off = 0,
           nr_syncs = 0,
           last_sync = 0;
    struct mueller_muller mm;
    const size_t samples_per_iter = TEST_MAX_NR_DECISIONS * TEST_SAMPLES_PER_BIT;
    uint32_t shr = 0;

    /*
     * Use the samples that were read in to test the Mueller-Muller clock recovery
     */

    TEST_ASSERT_OK(mm_init(&mm, TEST_KW, TEST_KM, TEST_SAMPLES_PER_BIT,
                TEST_SAMPLES_PER_BIT - TEST_ERROR_MARGIN, TEST_SAMPLES_PER_BIT + TEST_ERROR_MARGIN));

    while (samples_off < nr_samples) {
        size_t iter_samples = samples_per_iter > nr_samples - samples_off ? nr_samples - samples_off : samples_per_iter;
        TEST_ASSERT_OK(mm_process(&mm, samples + samples_off, iter_samples, decisions, 2 * TEST_MAX_NR_DECISIONS, &nr_decisions));

        samples_off += iter_samples;

        //TEST_INF("Iteration complete, got %zu samples, next = %f", nr_decisions, (double)mm.next_offset);

        for (size_t i = 0; i < nr_decisions; i++) {
            shr <<= 1;
            shr |= decisions[i] > 0 ? 0 : 1;
            if (__builtin_popcount(POCSAG_SYNC_CODEWORD ^ shr) < 4) {
                TEST_INF("Found sync word (decision %zu, delta %zu, block_offs %zu)!", total_decisions + i, total_decisions + i - last_sync, i);
                nr_syncs++;
                last_sync = total_decisions + i;
            } else if (0 != last_sync && total_decisions + i - last_sync == 544) {
                TEST_INF("Was expecting a sync word, got %08x", shr);
            }
        }

        total_decisions += nr_decisions;
    }

    TEST_INF("Total sync words: %zu", nr_syncs);
    TEST_INF("Total decisions: %zu", total_decisions);
    TEST_INF("Total samples: %zu", samples_off);

    TEST_ASSERT_EQUALS(nr_syncs, 9);

    return A_OK;
}

TEST_DECLARE_UNIT(test_smoke, mueller_muller)
{
    struct mueller_muller mm;

    /* Simple smoke test to test the init function */
    TEST_ASSERT_OK(mm_init(&mm, TEST_KW, TEST_KM, TEST_SAMPLES_PER_BIT,
                TEST_SAMPLES_PER_BIT - TEST_ERROR_MARGIN, TEST_SAMPLES_PER_BIT + TEST_ERROR_MARGIN));


    return A_OK;
}

TEST_DECLARE_SUITE(mueller_muller, test_mueller_muller_cleanup, test_mueller_muller_setup, NULL, NULL);

