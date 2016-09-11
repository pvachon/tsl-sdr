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
#include <stddef.h>

/* Forward declare opaque structures */
struct cpu_mask;
struct config;

/**
 * Create a new, empty CPU mask
 * \param mask reference to receive a pointer to the new mask
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_new(struct cpu_mask **mask);

/**
 * Clear a single node in the CPU mask
 * \param mask mask to update
 * \param cpu_id the CPU to clear
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_clear(struct cpu_mask *mask, size_t cpu_id);

/**
 * Set a single node in the CPU mask
 * \param mask mask to update
 * \param cpu_id the CPU to clear
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_set(struct cpu_mask *mask, size_t cpu_id);

/**
 * Clear entire CPU mask
 * \param mask mask to update
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_clear_all(struct cpu_mask *mask);

/**
 * Set entire CPU mask
 * \param mask mask to update
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_set_all(struct cpu_mask *mask);

/**
 * Test if a CPU is in the set
 * \param mask Mask to check
 * \param cpu_id The ID number of the CPU in question
 * \param value Reference to an integer to return the result in
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_test(struct cpu_mask *mask,
                        size_t cpu_id,
                        int *value);

/**
 * Clone a CPU mask
 * \param _new Reference to a new CPU mask
 * \param orig Original mask to clone
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_clone(struct cpu_mask **_new,
                         const struct cpu_mask *orig);

/**
 * Free a CPU mask
 * \param mask Reference to a pointer to existing CPU mask
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_delete(struct cpu_mask **mask);

/**
 * Apply a CPU mask
 * \param mask Applies the CPU mask to the current thread.
 * \param mask The CPU mask to be applied.
 * \return A_OK on success, an error code otherwise
 */
aresult_t cpu_mask_apply(struct cpu_mask *mask);


#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
