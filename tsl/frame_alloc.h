#pragma once

#include <tsl/result.h>

struct frame_alloc;

/**
 * Allocate a virtually contiguous chunk of memory that frames can be allocated from directly
 * by multiple threads.
 *
 * \param palloc The new allocator, returned by refernece
 * \param frame_bytes The size of a frame, in bytes
 * \param nr_frames The number of frames to create in this allocator.
 *
 * \return A_OK on success, an error code otherwise.
 *
 * \note This should be called from the thread deemed the "owner" of this object.
 */
aresult_t frame_alloc_new(struct frame_alloc **palloc, size_t frame_bytes, size_t nr_frames);

/**
 * Deallocate the frame region.
 *
 * \param palloc The allocator to deallocate.
 *
 * \note This should be called from the thread that created the object.
 */
aresult_t frame_alloc_delete(struct frame_alloc **palloc);

/**
 * Allocate a frame from the frame allocator
 *
 * \param alloc The allocator
 * \param pframe The frame that has been allocated
 *
 * \return A_OK on success, an error code otherwise. An error code indicates a programming bug.
 *
 * \note This can be called by any thread.
 */
aresult_t frame_alloc(struct frame_alloc *alloc, void **pframe);

/**
 * Free a frame from the frame allocator
 *
 * \param alloc The allocator
 * \param pframe The frame to be freed
 *
 * \return A_OK on success, an error code otherwise. An error code indicates a programming bug.
 *
 * \note This can be called by any thread.
 */
aresult_t frame_free(struct frame_alloc *alloc, void **pframe);

/**
 * Get the size of a frame, in bytes
 */
aresult_t frame_alloc_get_frame_size(struct frame_alloc *alloc, size_t *pframe_bytes);

/**
 * Get internal counter values from the frame alloc
 */
aresult_t frame_alloc_get_counts(struct frame_alloc *alloc, size_t *nr_frees, size_t *nr_allocs);

