#pragma once

#include <tsl/result.h>

struct ais_decode;

aresult_t ais_decode_new(struct ais_decode **pdecode, uint32_t freq);
aresult_t ais_decode_delete(struct ais_decode **pdecode);
aresult_t ais_decode_on_pcm(struct ais_decode *decode, const int16_t *samples, size_t nr_samples);

