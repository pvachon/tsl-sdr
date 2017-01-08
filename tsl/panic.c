/*
 *  panic.c - Application Panic Function
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
  Copyright (c) 2013, Phil Vachon <phil@cowpig.ca>
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
#include <tsl/panic.h>
#include <tsl/list.h>
#include <tsl/version.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <execinfo.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <unistd.h>

#define GET_TID()           ((int)syscall(SYS_gettid))

static
LIST_HEAD(panic_handlers);

CAL_NORETURN
void panic(const char *file, int line, const char *str, ...)
{
    va_list ap;

    struct panic_handler *hdlr = NULL;
    void *symbols[20];
    size_t len = 0;

    len = backtrace(symbols, BL_ARRAY_ENTRIES(symbols));

    /* Call each of the panic handlers */
    list_for_each_type(hdlr, &panic_handlers, pnode) {
        hdlr->func();
    }

#ifdef _AWESOME_PANIC_MESSAGE
    fprintf(stderr,
        "         _\n"
        "        /_/_      .'''.\n"
        "     =O(_)))) ...'     `.\n"
        "        \\_\\              `.    .'''\n"
        "                           `..'\n"
        "\n"
        "                 _\n"
        "                /_/_      .'''.\n"
        "             =O(_)))) ...'     `.\n"
        "                \\_\\              `.    .'''\n"
        "                                   `..'\n"
        "\n"
        "BEES. OH GOD, BEES EVERYWHERE"
        "\n"
        "Congratulations, your TSL Application PANICked. Here's what I know:\n");
#else /* if _AWESOME_PANIC_MESSAGE */
    fprintf(stderr, "The application has PANICked. Reason:\n");
#endif

    va_start(ap, str);
    vfprintf(stderr, str, ap);
    va_end(ap);

    fprintf(stderr, "\n\nApplication [version=%s] terminating at %s:%d [thread=%d]\n", tsl_get_version(), file, line, GET_TID());

    fprintf(stderr, "Call backtrace:\n");
    backtrace_symbols_fd(symbols, len, STDERR_FILENO);

    /* Generate a core dump if applicable */
    abort();
}

/* Add a panic handler */
void register_panic_handler(struct panic_handler *handler)
{
    list_init(&handler->pnode);
    list_append(&panic_handlers, &handler->pnode);
}
