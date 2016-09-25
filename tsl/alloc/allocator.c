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
/*
 * Implementation of the slab allocator subsystem
 * TODO: creation and management of pools of pinned pages
 */
#include <tsl/alloc/alloc_priv.h>
#include <tsl/alloc.h>
#include <tsl/list.h>
#include <tsl/assert.h>
#include <tsl/panic.h>
#include <app/app.h>
#include <config/engine.h>
#include <tsl/ticket_lock.h>
#include <tsl/diag.h>
#include <tsl/safe_alloc.h>

#include <stddef.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <sys/mman.h>
#include <unistd.h>

#define ALLOC_SLAB_NEXT_LINK                (struct slab_item *)0xfefef0f0f1f1f5f5ull

#define ALLOC_MSG(sev, sys, msg, ...) MESSAGE("ALLOC", sev, sys, msg, ##__VA_ARGS__)


/**
 * \brief Structure representing a particular class of slab.
 *
 * Slab classes represent a grouping of slabs with identical characteristics from
 * a system-level perspective.
 *
 * i.e. all items in the slab are pinned, or all slabs are virtual huge pages, etc.
 */
struct slab_class {
    /**
     * Slab class lock, since it's fairly infrequent that allocs go back to the main allocator
     */
    struct ticket_lock lock;
    struct list_entry avail_slabs;  /**< Number of available slabs */
    size_t slab_bytes;              /**< Number of bytes in a slab */
    size_t nr_slabs;                /**< Number of slabs in this class */
    void *slab_base_ptr;            /**< Pointer to the base of the slab region */
    size_t free_slabs;              /**< Number of free slabs */
};

struct slab_manager {
    struct slab_class normal_slabs;
    struct slab_class huge_slabs;
};

#define INIT_SLAB_CLASS(x) \
    { .avail_slabs = LIST_INIT(x.avail_slabs),  \
      .slab_bytes = 0,                          \
      .nr_slabs = 0,                            \
      .slab_base_ptr = NULL,                    \
      .free_slabs = 0,                          \
    }

/**
 * Round the given number to the nearest multiple of 16
 */
#define ROUND_16(x) ((((x) + 15) >> 4) << 4)

/**
 * Round the given number to the nearest multiple of a cache line
 */
#define ROUND_CACHE_LINE(x) ( ( ((x) + (SYS_CACHE_LINE_LENGTH - 1)) / SYS_CACHE_LINE_LENGTH ) * SYS_CACHE_LINE_LENGTH )


/* Private interface functions */
static struct slab_manager mgr = {
    .normal_slabs = INIT_SLAB_CLASS(mgr.normal_slabs),
    .huge_slabs = INIT_SLAB_CLASS(mgr.huge_slabs)
};

/* Evil */
int slab_manager_initialized = 0;

/* Initialize a slab allocator page class (by size) */
static
aresult_t __allocator_subsystem_init_page_class(struct slab_class *class,
                                                size_t count,
                                                size_t bytes,
                                                unsigned int flags)
{
    void *pages = NULL;

    if (count == 0) {
        /* No slabs of this class are to be allocated */
        return A_OK;
    }

    unsigned int nflags = flags | MAP_ANONYMOUS | MAP_PRIVATE;

    DIAG("Allocating %zd pages of size %zd (flags: 0x%08x)",
            count, bytes, flags);

    pages = mmap(NULL, count * bytes, PROT_READ | PROT_WRITE, nflags, -1, 0);

    if (pages == MAP_FAILED) {
        unsigned errnum = errno;
        PANIC("Allocation of slabs of size %zd failed (%u: %s). Aborting.", bytes, errno,
                strerror(errnum));
    }

    /* Now make the slabs available */
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        struct list_entry *page_start = (struct list_entry *)((char *)pages + offset);
        list_init(page_start);
        list_append(&class->avail_slabs, page_start);
        offset += bytes;
    }

    class->slab_bytes = bytes;
    class->nr_slabs = count;
    class->slab_base_ptr = pages;
    class->free_slabs = count;

    ticket_lock_init(&class->lock);

    return A_OK;
}

static
aresult_t __allocator_release_page(struct slab_class *class,
                                   void *ptr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(class != NULL);
    TSL_ASSERT_ARG(ptr != NULL);

    ticket_lock_acquire(&class->lock);

    struct list_entry *slab = (struct list_entry *)ptr;

    list_append(&class->avail_slabs, slab);

    ticket_lock_release(&class->lock);

    return ret;
}

static
aresult_t __allocator_acquire_page(struct slab_class *class,
                                   void **ptr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(class != NULL);
    TSL_ASSERT_ARG(ptr != NULL);

    ticket_lock_acquire(&class->lock);

    if (CAL_UNLIKELY(list_empty(&class->avail_slabs))) {
        ret = A_E_NOMEM;
        goto done;
    }

    struct list_entry *item = LIST_NEXT(&class->avail_slabs);

    list_del(item);

    *ptr = (void *)item;

done:
    ticket_lock_release(&class->lock);
    return ret;
}

aresult_t allocator_system_init(size_t nr_slabs, size_t page_size, size_t nr_huge_slabs, size_t huge_page_bytes)
{
    aresult_t ret = A_OK;

    if (0 < nr_slabs) {
        if (FAILED(ret =
            __allocator_subsystem_init_page_class(&mgr.normal_slabs, nr_slabs, page_size, 0)))
        {
            goto done;
        }
    }

    if (0 < nr_huge_slabs) {
        if (FAILED(ret =
            __allocator_subsystem_init_page_class(&mgr.huge_slabs, nr_huge_slabs, huge_page_bytes, MAP_HUGETLB)))
        {
            goto done;
        }
    }


    slab_manager_initialized = 1;

done:
    return ret;
}

/** \brief Initialize a slab
 * Initialize a slab to empty state. All attributes are aligned on a
 * 16-byte boundary to facilitate use of SSE where possible.
 * \note This could be more clever. For example, the slab could have two states:
 * one where the items are picked from the free list, the other where the
 * new item is taken from a specified offset in the slab. This avoids the
 * need to initialize the list of all free items to start with.
 * \param slab_base The base address for the new slab
 * \param obj_size The size of the objects to be allocated in this slab, rounded up 16
 * \param slab_size The size of the slab, in bytes
 */
static
struct slab *__helper_slab_init(void *slab_base,
                                size_t obj_size,
                                size_t slab_size)
{
    struct slab *slab = (struct slab *)slab_base;
    /* Round first object offset to nearest cache line */
    size_t first_obj_offset = ROUND_CACHE_LINE(sizeof(struct slab));
    struct slab_item *first = NULL;

    list_init(&slab->snode);
    slab->flags = 0x0;

    size_t item_count = (slab_size - first_obj_offset)/obj_size;
    slab->free_item_count = item_count;
    slab->max_items = item_count;
    slab->slab_size = slab_size;

    first = slab_base + first_obj_offset;

    /* Set up linked list of free items */
    first->next = ALLOC_SLAB_NEXT_LINK;
    slab->free_items = first;

    return slab;
}

static inline
struct allocator *__helper_allocator_new_init(void)
{
    struct allocator *new_alloc = NULL;

    new_alloc = (struct allocator *)calloc(1, sizeof(struct allocator));

    if (new_alloc) {
        list_init(&new_alloc->slabs);
        new_alloc->item_size = 0;
        new_alloc->max_items = 0;
        new_alloc->free_mask = 0;
    }

    return new_alloc;
}

static inline
void __helper_add_slab(struct allocator *alloc,
                       struct slab *slab)
{
    list_prepend(&alloc->slabs, &slab->snode);
    alloc->max_items += slab->free_item_count;
}

/* Public interface functions */

aresult_t allocator_new(struct allocator **alloc, size_t item_size, size_t item_count, uint32_t flags)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(alloc != NULL);
    TSL_ASSERT_ARG(item_size != 0);
    TSL_ASSERT_ARG(item_count != 0);

    *alloc = NULL;

    /* Round the item size to the nearest 16 bytes */
    size_t real_size = ROUND_16(item_size);

    struct slab_class *cls = &(mgr.normal_slabs);
    if (ALLOC_FLAG_HUGE_PAGE & flags) {
        cls = &(mgr.huge_slabs);
    }

    size_t page_size = cls->slab_bytes;
    size_t items_per_slab = page_size / real_size;

    if (items_per_slab == 0) {
        DIAG("Zero items per slab.");
        ret = A_E_NOMEM;
        goto done;
    }

    struct allocator *new_alloc = NULL;
    *alloc = NULL;

    new_alloc = __helper_allocator_new_init();

    if (new_alloc == NULL) {
        ret = A_E_NOMEM;
        goto done;
    }

    new_alloc->item_size = real_size;
    new_alloc->free_mask = ~(page_size - 1);
    new_alloc->alloc_state = cls;

    DIAG("New Allocator: requested item_size = %zd, real_size = %zd, free_mask = 0x%016zx",
            item_size, new_alloc->item_size, new_alloc->free_mask);

    /* Determine how many pages we need to allocate */
    size_t num_pages = (item_count + items_per_slab - 1)/items_per_slab;

    DIAG("New Allocator: will be allocating %zd pages.", num_pages);

    /* Grab the pages from the slab manager */
    for (size_t i = 0; i < num_pages; ++i) {
        void *new_page = NULL;
        aresult_t page_result = __allocator_acquire_page(cls, &new_page);

        if (FAILED(page_result)) {
            ret = page_result;
            DIAG("Could not acquire allocator page.");
            goto done_cleanup;
        }

        struct slab *new_slab = __helper_slab_init(new_page,
                                                   new_alloc->item_size,
                                                   page_size);

        /* Add the slab to the list */
        __helper_add_slab(new_alloc, new_slab);
    }

    *alloc = new_alloc;

done_cleanup:
    if (FAILED(ret)) {
        DIAG("Failure during allocator allocation. Should free memory!");
    }

done:
    return ret;
}

/* Grow an allocator by a single page */
static
aresult_t __helper_allocator_grow(struct allocator *alloc)
{
    aresult_t ret = A_OK;
    struct slab_class *cls = (struct slab_class *)alloc->alloc_state;

    void *new_page = NULL;
    ret = __allocator_acquire_page(cls, &new_page);

    if (FAILED(ret)) {
        goto done;
    }

    struct slab *new_slab = __helper_slab_init(new_page,
                                               alloc->item_size,
                                               cls->slab_bytes);

    alloc->slabs_taken++;

    /* Add the slab to the list */
    __helper_add_slab(alloc, new_slab);
done:
    return ret;
}

/* Shrink an allocator by removing the specified slab */
static
aresult_t __helper_allocator_shrink(struct allocator *alloc,
                                    struct slab *slab)
{
    aresult_t ret = A_OK;
    struct slab_class *cls = (struct slab_class *)alloc->alloc_state;

    if (slab->free_item_count != slab->max_items) {
        DIAG("Attempted to free a non-empty slab!");
        ret = A_E_BADARGS;
        goto done;
    }

    list_del(&slab->snode);

    ret = __allocator_release_page(cls, slab);
done:
    return ret;
}

aresult_t allocator_alloc(struct allocator *alloc, void **item_ptr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(alloc != NULL);
    TSL_ASSERT_ARG(item_ptr);

    int growable = !(alloc->flags & ALLOC_FLAG_NO_GROW);

    *item_ptr = NULL;

    /* Sanity check to make sure the allocator has some space */
    struct slab *first = LIST_ITEM(LIST_NEXT(&alloc->slabs), struct slab, snode);
    if (CAL_UNLIKELY(list_empty(&alloc->slabs) || (first->free_item_count == 0))) {
        if (growable) {
            __helper_allocator_grow(alloc);
            /* Grab the first slab again */
            first = LIST_ITEM(LIST_NEXT(&alloc->slabs), struct slab, snode);
        } else {
            ret = A_E_NOMEM;
            goto done;
        }
    }

    if (CAL_UNLIKELY(first->free_item_count == 0)) {
        /* Something went really bad */
        ret = A_E_NOMEM;
        goto done;
    }

    struct slab_item *item = first->free_items;
    if (NULL == item) {
        DIAG("Somehow the free item list for this allocator is set to NULL");
        ret = A_E_NOMEM;
        goto done;
    }

    if (ALLOC_SLAB_NEXT_LINK == item->next) {
        /* Fill in the next item */
        struct slab_item *next = ((void *)item) + alloc->item_size;

        if (((void *)next + alloc->item_size) <= ((void *)first + first->slab_size)) {
            next->next = ALLOC_SLAB_NEXT_LINK;
        } else {
            /* End of the pool of items */
            next = NULL;
        }

        item->next = next;
    }

    first->free_items = item->next;
    item->next = NULL;

    first->free_item_count--;

    if (CAL_UNLIKELY(first->free_item_count == 0)) {
        /* If the free item count dropped to 0, move to the back of the list */
        list_del(&first->snode);
        list_append(&alloc->slabs, &first->snode);
    }

    *item_ptr = item;

done:
    return ret;
}

aresult_t allocator_free(struct allocator *alloc,
                         void **item_ptr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(alloc != NULL);
    TSL_ASSERT_ARG(item_ptr != NULL);
    TSL_ASSERT_ARG(*item_ptr != NULL);

    void *item = *item_ptr;

    /* Find our slab (yay for page alignment rules) */
    struct slab *slab = (struct slab *)((size_t)item & alloc->free_mask);

    /* Return item to the free list */
    struct slab_item *returned = (struct slab_item *)item;
    returned->next = slab->free_items;
    slab->free_items = returned;

    slab->free_item_count++;

    if (CAL_UNLIKELY(slab->free_item_count == slab->max_items)) {
        if (alloc->slabs.next != alloc->slabs.prev) {
            DIAG("Allocator %p returning slab %p to free page list", alloc, slab);
            ret = __helper_allocator_shrink(alloc, slab);
        }
    } else {
        /* Move this slab to the head of the list */
        list_del(&slab->snode);
        list_prepend(&alloc->slabs, &slab->snode);
    }


    *item_ptr = NULL;

    return ret;
}

/**
 * \note This might be encumbered by an IBM patent
 */
aresult_t allocator_squeeze(struct allocator *alloc)
{
    aresult_t ret = A_OK;

    return ret;
}

aresult_t allocator_delete(struct allocator **alloc)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(alloc != NULL);
    TSL_ASSERT_ARG(*alloc != NULL);

    struct allocator *dalloc = *alloc;
    struct slab_class *cls = (struct slab_class *)dalloc->alloc_state;

    TSL_ASSERT_ARG(cls != NULL);

    if (!list_empty(&dalloc->slabs)) {
        struct slab *slab = NULL, *tmp = NULL;
        list_for_each_type_safe(slab, tmp, &dalloc->slabs, snode) {
            if (slab->max_items != slab->free_item_count) {
                /* Can't kill an allocator that is in use */
                ret = A_E_BUSY;
                DIAG("Slab at 0x%p has items in use.", slab);
                goto done;
            }

            list_del(&slab->snode);
            dalloc->max_items -= slab->max_items;
            __allocator_release_page(cls, slab);
        }
    }

    if (!list_empty(&dalloc->slabs)) {
        DIAG("Allocator deletion failed -- there are extraneous slabs!");
        ret = A_E_BUSY;
        goto done;
    }

    DIAG("Allocator at %p destroyed. Allocator used %u auxiliary slabs during lifetime.", dalloc, dalloc->slabs_taken);

    memset(dalloc, 0, sizeof(struct allocator));
    TFREE(*alloc);

done:
    return ret;
}
