#include <pager/pager.h>
#include <pager/pager_pocsag.h>
#include <pager/pager_pocsag_priv.h>

#include <tsl/result.h>

aresult_t pager_pocsag_new(struct pager_pocsag **ppocsag, uint32_t freq_hz, pager_pocsag_on_numeric_msg_func_t on_numeric,
        pager_pocsag_on_alpha_msg_func_t on_alpha);
aresult_t pager_pocsag_delete(struct pager_pocsag **ppocsag);
aresult_t pager_pocsag_on_pcm(struct pager_pocsag *pocsag, const int16_t *pcm_samples, size_t nr_samples);
