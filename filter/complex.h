#pragma once

#include <filter/filter.h>

/**
 * Round the given fixed point Q.30 value to a Q.15 value
 */
static inline
uint16_t round_q30_q15(uint32_t a)
{
    return ((a >> Q_15_SHIFT) + ((a >> (Q_15_SHIFT - 1)) & 1));
}

/**
 * Calculate the product of 2 q15 values, and return as q30. This maintains maximum information
 * possible.
 */
static inline
void cmul_q15_q30(int16_t a_re, int16_t a_im, int16_t b_re, int16_t b_im,
                  int32_t *r_re, int32_t *r_im)
{
    *r_re = a_re * b_re - a_im * b_im;
    *r_im = a_re * b_im + a_im * b_re;
}

/**
 * Calculate the product of 2 q15 values, and return as q15.
 */
static inline
void cmul_q15_q15(int16_t a_re, int16_t a_im, int16_t b_re, int16_t b_im,
                  int16_t *r_re, int16_t *r_im)
{
    *r_re = (a_re * b_re - a_im * b_im) >> Q_15_SHIFT;
    *r_im = (a_re * b_im + a_im * b_re) >> Q_15_SHIFT;
}

