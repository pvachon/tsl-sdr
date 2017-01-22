/*
 *  complex.c - Numerical Helpers for Q.15 Fixed Point Arithmetic
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#pragma once

#include <filter/filter.h>

/**
 * Round the given fixed point Q.30 value to a Q.15 value
 */
static inline
int16_t round_q30_q15(int32_t a)
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
    int32_t a_re_32 = a_re,
            a_im_32 = a_im,
            b_re_32 = b_re,
            b_im_32 = b_im;

    *r_re = round_q30_q15(a_re_32 * b_re_32 - a_im_32 * b_im_32);
    *r_im = round_q30_q15(a_re_32 * b_im_32 + a_im_32 * b_re_32);
}

