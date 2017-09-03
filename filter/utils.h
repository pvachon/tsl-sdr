#pragma once

#include <tsl/result.h>

#include <stdint.h>

struct sample_buf;

aresult_t dot_product_sample_buffers_real(struct sample_buf *sb_active,
        struct sample_buf *sb_next, size_t buf_start_offset,
        int16_t *coeffs, size_t nr_coeffs, int16_t *psample);

aresult_t dot_product_sample_buffers_complex(
        struct sample_buf *sb_active,
        struct sample_buf *sb_next,
        size_t buf_start_offset,
        int16_t *coeffs,
        size_t nr_coeffs,
        int16_t *psample_re,
        int16_t *psample_im);
