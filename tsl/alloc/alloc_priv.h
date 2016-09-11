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

#include <tsl/list.h>
#include <tsl/alloc.h>

/** \file allocator_priv.h
 * Private state for the slab allocator used by the The Standard Library
 */

/** \brief Allocator state structure
 * Structure representing the internal state of an allocator.
 */
struct allocator {
    /** The list of slabs */
    struct list_entry slabs;
    /** Size of allocated item */
    size_t item_size;
    /** Maximum count of items */
    size_t max_items;
    /** Mask used to get the slab metadata pointer */
    size_t free_mask;
    /** Particular allocator substate */
    void *alloc_state;
    /** Flags for allocator state */
    unsigned int flags;
    /** Number of times we had to go back to the free slab pool */
    unsigned int slabs_taken;
};

#define SLAB_FLAG_SQUEEZE 0x1   /**< Mark a slab to be squeezed */

struct slab_item {
    /**
     * Next slab item to be allocated.
     */
    struct slab_item *next;
};

/** \brief Structure representing a slab's metadata
 * A structure that contains all the metadata for a slab, including a
 * pointer to the first entry in the slab.
 */
struct slab {
    struct list_entry snode;            /**< Linked list entry of slabs */
    struct slab_item *free_items;       /**< Linked list of free items in slab */
    uint32_t flags;                     /**< Flags associated with the slab */
    uint32_t max_items;                 /**< Maximum items you can have in this slab */
    uint32_t free_item_count;           /**< Count of free items in this slab */
    uint32_t slab_size;                 /**< Size of the slab, in bytes */
};

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
