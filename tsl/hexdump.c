/*
 *  hexdump.c - Simple hexdump routine. Useful for debugging.
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

/*
  Copyright (c) 2016, 12Sided Technology, LLC
  Author: Phil Vachon <pvachon@12sidedtech.com>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  - Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.

  - Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
  OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
  WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <tsl/hexdump.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>

#include <ctype.h>
#include <stdio.h>

aresult_t hexdump_dumpf_hex(FILE* f, const void *buf, size_t length)
{
    aresult_t ret = A_OK;

    if (NULL == f) {
        return A_OK;
    }

    const uint8_t *ptr = buf;

    TSL_ASSERT_ARG(NULL != buf);
    TSL_ASSERT_ARG(0 < length);

    fprintf(f, "Dumping %zu bytes at %p\n", length, buf);

    for (size_t i = 0; i < length; i+=16) {
        fprintf(f, "%16zx: ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < length) {
                fprintf(f, "%02x ", (unsigned)ptr[i + j]);
            } else {
                fprintf(f, "   ");
            }
        }
        fprintf(f, " |");
        for (int j = 0; j < 16; j++) {
            if (i + j < length) {
                fprintf(f, "%c", isprint(ptr[i + j]) ? (char)ptr[i + j] : '.');
            } else {
                fprintf(f, " ");
            }
        }
        fprintf(f, "|\n");
    }

    return ret;
}

aresult_t hexdump_dump_hex(const void *buf, size_t length)
{
    return hexdump_dumpf_hex(stdout, buf, length);
}

