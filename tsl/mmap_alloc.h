#pragma once

#include <tsl/list.h>
/* #include <config/engine.h> */
#include <tsl/result.h>

#define MMAP_ALLOC_MODE_CREATE (1)
#define MMAP_ALLOC_MODE_LOAD   (2)

/**
 * fixed size allocator that employs a free-list.
 * It has a fixed upper bound as well. It does not grow.
 */
struct mmap_alloc;

/**
 *  For the construction of an allocator without an app config
 */
aresult_t mmap_alloc_configured(uint32_t num_buffers,
                                uint32_t buffer_size,
                                uint8_t mode,
                                const char* shmfile,
                                struct mmap_alloc** alloc);

/**
 *  Delete the allocator, if it is creator mode, then it will unlink the shm as well.
 */
aresult_t mmap_alloc_delete(struct mmap_alloc** alloc);

/**
 *  Return a chunk of data, the first 16 bytes of the buffer are reserved, it is guaranteed
 *  to have at leaste enough data to provide buffer_size;
 *  Note:  It will find the next power of 2 that can contain buffer_size + 16.
 *  So if you pass it 4096, it will create a buffer that is 8192,
 *  so if you want a 1 page buffer, pass it some number between 2048 and 4080
 */
aresult_t mmap_alloc_alloc(struct mmap_alloc*, uint8_t** data);

/**
 * Send a chunk of data back to the ether whence it came.
 */
aresult_t mmap_alloc_free(struct mmap_alloc*, uint8_t** data);

/**
 * Sets a provided pointer to point to the chunk of data provided by the offset
 */
static inline
aresult_t mmap_alloc_get_ptr(struct mmap_alloc*, uint64_t offset, uint8_t** pointer);

static inline
aresult_t mmap_alloc_get_offset(struct mmap_alloc*, uint8_t* pointer, uint64_t* offset);

#include <tsl/alloc/mmap_alloc_priv.h>
