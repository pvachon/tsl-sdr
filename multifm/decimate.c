#include <multifm/decimate.h>
#include <multifm/sambuf.h>

#include <tsl/result.h>
#include <tsl/errors.h>
#include <tsl/assert.h>
#include <tsl/diag.h>

#include <string.h>

aresult_t decimate_init(struct decimate *decim, uint32_t scale_factor)
{
    aresult_t ret = A_OK;

    /* Zero it out */
    memset(decim, 0, sizeof(struct decimate));

    if (1 != tsl_pop_count_64(scale_factor)) {
        DIAG("Invalid scale factor. Must be a power of two.");
        ret = A_E_BADARGS;
        goto done;
    }

    decim->shift_size = 32 - tsl_bit_scan_rev_64(scale_factor);

    DIAG("Decimating by a factor of %u (log2: %u)\n", scale_factor, decim->shift_size);

done:
    return ret;
}

/**
 * Decimate the given input sample buffer into the output buffer. When
 * this function returns, there will be one of two reasons:
 *
 *  - The input buffer is empty
 *  - The output buffer is full.
 *
 * The caller can easily determine which by checking the current offset
 * in the output buffer, and the current offset in the input buffer.
 *
 * \note It's possible for BOTH events to occur simultaneously.
 *
 * \param decim The decimator state.
 * \param in The input sample buffer. This is usually at full sample rate.
 * \param out The output sample buffer. This is at the decimated rate.
 *
 * \return A_OK on success, an error code otherwise.
 *
 * \note This effectively decimates using a boxcar function. The boxcar
 *       function 
 */
aresult_t decmiate_process(struct decimate *decim,
        struct sample_buf *in,
        struct sample_buf *out)
{
    aresult_t ret = A_OK;
    int16_t *in_sample_ptr = NULL,
            *out_sample_ptr = NULL;
    size_t nr_samples_decimate = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != decim);
    TSL_ASSERT_ARG_DEBUG(NULL != in);
    TSL_ASSERT_ARG_DEBUG(NULL != out);

    in_sample_ptr = (int16_t *)in->data_buf;
    out_sample_ptr = (int16_t *)out->data_buf;
    nr_samples_decimate = 1ull << decim->shift_size;

    /* Iterate through the output buffer */
    for (size_t i = out->sample_cur_loc;
            out->sample_cur_loc < out->nr_samples;
            out++)
    {
        size_t start = in->sample_cur_loc;

        /* Walk the input buffer in full chunks */
        for (size_t j = start;
                decim->nr_samples < nr_samples_decimate && j < in->nr_samples;
                j++)
        {
            decim->re_val += in_sample_ptr[2 * j];
            decim->im_val += in_sample_ptr[2 * j + 1];
            decim->nr_samples++;
            in->sample_cur_loc++;
        }

        if (CAL_UNLIKELY(nr_samples_decimate << decim->shift_size)) {
            /* Don't deposit a sample, we didn't have enough inputs */
            break;
        }

        /* Write the output decimated sample */
        out_sample_ptr[2 * i] = decim->re_val;
        out_sample_ptr[2 * i + 1] = decim->im_val;
        out->sample_cur_loc++;

        /* Reset the intermediate state */
        decim->re_val = 0;
        decim->im_val = 0;
        decim->nr_samples = 0;
    }

    return ret;
}

