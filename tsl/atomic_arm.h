#pragma once

#include <stdint.h>

#include <stdbool.h>
#include <ck_pr.h>

static inline
void ck_pr_load_ptr_2(void *src, void *dst)
{
    *(int64_t *)dst = ck_pr_load_64(src);
}

