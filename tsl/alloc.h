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

#include <tsl/version.h>
#include <tsl/errors.h>

#include <stdint.h>
#include <stdlib.h>

#define ALLOC_FLAG_NO_GROW          0x1     /** Tag given allocator as non-growable */
#define ALLOC_FLAG_HUGE_PAGE        0x2     /** Allocator steals from huge page pool */

struct allocator;

/** \brief Create a new allocator
 * Create and initialize a new allocator, setup to contain at least item_count
 * items of size item_size.
 */
aresult_t allocator_new(struct allocator **alloc,
                        size_t item_size,
                        size_t item_count,
                        uint32_t flags);

/** \brief Allocate an item from the allocator
 * Take an item from the free list of the allocator, update the slab list if need be
 * and return a pointer to the free item.
 * If the allocator is tagged as being non-growable, will return a NOMEM if it is
 * unable to allocate memory. Otherwise, returns OK and populates the reference to
 * the new memory pointer.
 */
aresult_t allocator_alloc(struct allocator *alloc,
                          void **item_ptr);

/** \brief Free an item from the allocator
 * Release an item that was previously allocated. Updatres item_ptr as NULL on success.
 */
aresult_t allocator_free(struct allocator *alloc,
                         void **item_ptr);

/** \brief Squeeze an allocator
 * If the allocator is growable, tag all slabs that have less than 50% fill to be
 * squeezed. When the slab is empty, it is removed from the available slab list for
 * the given allocator and returned to the slab manager.
 * If the allocator is non-growable, does nothing -- will generate an assertion in DEBUG.
 * \param alloc The allocator to squeeze
 * \return A_OK on success, an error code otherwise.
 */
aresult_t allocator_squeeze(struct allocator *alloc);

/** \brief Release an allocator
 * Destroy an allocator and release its used slabs to the page pool.
 * \note If the allocator has pages with items in use, the release will fail.
 */
aresult_t allocator_delete(struct allocator **alloc);

/** \brief Initialize the allocator subsystem
 *
 * \param nr_slabs The number of slabs
 * \param page_size The size of the pages
 * \param nr_huge_slabs Number of huge slabs
 * \param huge_page_bytes size of huge pages
 */
aresult_t allocator_system_init(size_t nr_slabs,
                                size_t page_size,
                                size_t nr_huge_slabs,
                                size_t huge_page_bytes);

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
