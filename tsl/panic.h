#pragma once
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

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

#include <tsl/list.h>
#include <tsl/cal.h>

/** Function to be invoked when a panic occurs */
typedef void (*panic_handler_func_t)(void);

/** Structure representing a panic handler */
struct panic_handler {
    panic_handler_func_t func;
    struct list_entry pnode;
};

/* Force a panic of the application, with a specified message */
CAL_NORETURN
void panic(const char *file, int line, const char *str, ...);

#define PANIC(message, ...) \
    panic(__FILE__, __LINE__, message, ##__VA_ARGS__)

/**
 * Register a function to be called on a panic (for cleanup or otherwise)
 * \param handler Pointer to a static panic handler.
 * \note handler should not reside on the heap, but rather be in .data.
 */
void register_panic_handler(struct panic_handler *handler);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
