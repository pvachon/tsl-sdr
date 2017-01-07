#pragma once

#include <filter/filter.h>
#include <tsl/result.h>
#include <stdbool.h>

struct sample_buf;
struct polyphase_fir;

aresult_t polyphase_fir_new(struct polyphase_fir **pfir, size_t nr_coeffs, const int16_t *fir_real_coeff,
        unsigned interpolate, unsigned decimate);
aresult_t polyphase_fir_delete(struct polyphase_fir **pfir);
aresult_t polyphase_fir_push_sample_buf(struct polyphase_fir *fir, struct sample_buf *buf);
aresult_t polyphase_fir_process(struct polyphase_fir *fir, int16_t *out_buf, size_t nr_out_samples,
        size_t *nr_out_samples_generated);
aresult_t polyphase_fir_can_process(struct polyphase_fir *fir, bool *pcan_process);
aresult_t polyphase_fir_full(struct polyphase_fir *fir, bool *pfull);

