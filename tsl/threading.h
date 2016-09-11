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

#include <tsl/errors.h>

/* Forward declaration */
struct thread;
struct cpu_mask;

typedef aresult_t (*thread_function_t)(void *params);

/** \brief Create a new thread
 * Create a new thread and state for the given thread, but do not kick it off
 * yet.
 * \param thread Reference to a pointer to a structure that will be populated with
 *               a struct containing thread state
 * \param func The function to be dispatched in the new thread.
 * \param mask The CPU affinity mask. The struct thread will take ownership of this.
 */
aresult_t thread_create(struct thread **thread,
                        thread_function_t func,
                        struct cpu_mask *mask);

/** \brief Check if a thread is running
 * Return whether or not a thread is running
 */
aresult_t thread_running(struct thread *thread,
                         int *val);

/** \brief Start a thread.
 * Take a thread that is not running and start it, with the provided arguments
 * structure.
 */
aresult_t thread_start(struct thread *thread,
                       void *params);

/** \brief Join a thread
 * Wait for a thread to return and join to the parent thread.
 * \param thread The thread to join to the current thread
 * \param result The result returned from the thread
 * \note If not used correctly, this could cause a deadlock
 */
aresult_t thread_join(struct thread *thread,
                      aresult_t *result);

/** \brief Release a dead thread
 * If a given thread is dead, release the thread and associated resources
 */
aresult_t thread_destroy(struct thread **thread);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
