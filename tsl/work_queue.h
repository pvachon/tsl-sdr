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

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/safe_alloc.h>

#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <ck_ring.h>
#pragma GCC diagnostic pop

#include <string.h>
#include <stdlib.h>

/**
 * Structure representing a work queue. The contents of this structure are private,
 * do not touch the members of the structure. Use the provided accessors!
 * \note The work_queue structure is intended to be embeddable in another structure.
 */
struct work_queue {
    ck_ring_t fifo;

    size_t max_items;
    ck_ring_buffer_t *buffer;
};

/**
 * Structure tagged for a SPMC work queue -- mostly a convenience structure
 */
struct work_queue_spmc {
    struct work_queue q;
};

/**
 * Structure tagged for an MPMC work queue.
 */
struct work_queue_mpmc {
    struct work_queue q;
};

static inline
aresult_t work_queue_new(struct work_queue *queue, size_t max_items)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(queue != NULL);
    TSL_ASSERT_ARG(max_items > 4);
    TSL_ASSERT_ARG((max_items & (max_items - 1)) == 0);

    memset(queue, 0, sizeof(*queue));

    queue->buffer = calloc(max_items + 1, sizeof(ck_ring_buffer_t));

    if (queue->buffer == NULL) {
        DIAG("Unable to allocate queue buffer for %zu items.", max_items);
        return A_E_NOMEM;
    }

    queue->max_items = max_items;

    ck_ring_init(&queue->fifo, queue->max_items);

    return ret;
}

static inline
aresult_t work_queue_release(struct work_queue *queue)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(queue != NULL);

    TFREE(queue->buffer);
    memset(queue, 0, sizeof(*queue));

    return ret;
}

static inline
aresult_t work_queue_push(struct work_queue *queue, void *ptr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(queue != NULL);
    TSL_ASSERT_ARG(ptr != NULL);
    TSL_ASSERT_ARG(queue->buffer != NULL);

    if (ck_ring_enqueue_spsc(&queue->fifo, queue->buffer, ptr) == false) {
        ret = A_E_BUSY;
        goto done;
    }

done:
    return ret;
}

static inline
aresult_t work_queue_pop(struct work_queue *queue, void **ptr)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(queue != NULL);
    TSL_ASSERT_ARG(ptr != NULL);
    TSL_ASSERT_ARG(queue->buffer != NULL);

    if (ck_ring_dequeue_spsc(&queue->fifo, queue->buffer, ptr) == false) {
        *ptr = NULL;
        goto done;
    }

done:
    return ret;
}

/**
 * Return the number of entries populated in the work queue
 */
static inline
aresult_t work_queue_fill(struct work_queue *queue, unsigned int *fill)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(queue);
    TSL_ASSERT_ARG(fill);
    TSL_ASSERT_ARG(queue->buffer != NULL);

    *fill = ck_ring_size(&queue->fifo);

    return ret;
}

static inline
aresult_t work_queue_size(struct work_queue *queue, unsigned int *size)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(queue);
    TSL_ASSERT_ARG(size);
    TSL_ASSERT_ARG(queue->buffer != NULL);

    *size = ck_ring_capacity(&queue->fifo);

    return ret;
}

/**
 * Create a new MPMC queue.
 */
static inline
aresult_t work_queue_mpmc_new(struct work_queue_mpmc *q, size_t nr_items)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != q);

    ret = work_queue_new(&q->q, nr_items);

    return ret;
}

static inline
aresult_t work_queue_mpmc_release(struct work_queue_mpmc *q)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != q);

    ret = work_queue_release(&q->q);

    return ret;
}

static inline CAL_AGGRESSIVE_INLINE
aresult_t work_queue_mpmc_push(struct work_queue_mpmc *q, void *message)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != q);

    if (false == ck_ring_enqueue_mpmc(&q->q.fifo, q->q.buffer, message)) {
        ret = A_E_BUSY;
    }

    return ret;
}

static inline CAL_AGGRESSIVE_INLINE
aresult_t work_queue_mpmc_pop(struct work_queue_mpmc *q, void **pmessage)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != pmessage);

    if (false == ck_ring_dequeue_spmc(&q->q.fifo, q->q.buffer, pmessage)) {
        *pmessage = NULL;
    }

    return ret;
}

/**
 * Create a new SPMC queue.
 *
 * \param q Pointer to memory to be initialized as a new SPMC work queue
 * \param nr_items Number of items. Must be a power of two.
 *
 * \return A_OK on success, an error code otherwise
 */
static inline
aresult_t work_queue_spmc_new(struct work_queue_spmc *q, size_t nr_items)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != q);

    ret = work_queue_new(&q->q, nr_items);

    return ret;
}

/**
 * Push an item into an SPMC queue.
 *
 * \note Function is called from the producer's context only.
 *
 * \param q The queue to push the message into
 * \param message The message the push into the queue
 *
 * \return A_OK on success, an error code otherwise.
 */
static inline
aresult_t work_queue_spmc_push(struct work_queue_spmc *q, void *message)
{
    aresult_t ret = A_OK;

    if (false == ck_ring_enqueue_spmc(&q->q.fifo, q->q.buffer, message)) {
        ret = A_E_BUSY;
    }

    return ret;
}

/**
 * Pop an item out of an SPMC queue
 *
 * \note Function can be safely called from any context.
 *
 * \param q The queue to pop the message from.
 * \param message The returned message. NULL if no messages available.
 *
 * \return A_OK on success, an error code otherwise.
 */
static inline
aresult_t work_queue_spmc_pop(struct work_queue_spmc *q, void **message)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != q);
    TSL_ASSERT_ARG_DEBUG(NULL != message);

    if (false == ck_ring_dequeue_spmc(&q->q.fifo, q->q.buffer, message)) {
        *message = NULL;
    }

    return ret;
}

static inline
aresult_t work_queue_spmc_release(struct work_queue_spmc *q)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(NULL != q);

    ret = work_queue_release(&q->q);

    return ret;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
