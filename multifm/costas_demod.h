#pragma once

#include <tsl/result.h>

struct demod_base;

aresult_t multifm_costas_demod_init(struct demod_base **pdemod, float f_shift, float alpha, float beta, int16_t e_max);
aresult_t multifm_costas_demod_process(struct demod_base *demod, int16_t *in_samples, size_t nr_in_samples,
        int16_t *out_samples, size_t *pnr_out_samples, size_t *pnr_out_bytes);
aresult_t multifm_costas_demod_cleanup(struct demod_base **pdemod);

