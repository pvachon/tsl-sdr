#pragma once

#include <tsl/result.h>

struct bch_code;

aresult_t bch_code_new(struct bch_code **pcode, int p[], int m, int n, int k, int t);
void bch_code_delete(struct bch_code **bch_code_data);
void bch_code_encode(struct bch_code *bch_code_data, int data[]);
int bch_code_decode(struct bch_code *bch_code_data, uint32_t *precd);

