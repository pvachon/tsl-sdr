#pragma once

/**
 * TSL-friendly helper macros for calling the libc dynamic allocator.
 *
 * These macros attempt to check if invalid parameter types are being used to feed the dynamic allocator sizes. As well,
 * the macros will return TSL-style error codes, rather than simply NULL or otherwise, in the event of a failure. Finally,
 * the underlying functions will prevent the infamous "allocate of size 0" error.
 */

#include <tsl/result.h>
#include <tsl/cal.h>
#include <tsl/panic.h>

#include <stddef.h>

#define __SAFE_IS_UL(_v) __builtin_types_compatible_p(__typeof__(_v), size_t)
#define __SAFE_FAIL(_v) __builtin_choose_expr(__SAFE_IS_UL(_v), A_OK, PANIC("Attempt to alloc without a size_t!"))
#define __SAFE_FAIL2(_v1, _v2) __builtin_choose_expr(__SAFE_IS_UL(_v1) && __SAFE_IS_UL(_v2), A_OK, PANIC("Attempt to alloc without a size_t"))

/**
 * A somewhat sanity checked malloc(3)
 */
#define TMALLOC(_ptr, _sz) __builtin_choose_expr(__SAFE_IS_UL(_sz), __safe_malloc((_ptr), (_sz)), __SAFE_FAIL(_sz))

/**
 * A somewhat sanity-checked calloc(3)
 */
#define TCALLOC(_ptr, _ct, _sz) __safe_calloc((_ptr), (_ct), (_sz))

/**
 * A somewhat sanity-checked realloc(3)
 */
#define TREALLOC(_ptr, _sz) __builtin_choose_expr(__builtin_types_compatible_p(__typeof__(_sz), size_t), __safe_realloc((_ptr), (_sz)), __SAFE_FAIL(_sz))

/**
 * calloc(3)-style callable, with alignment
 */
#define TACALLOC(_ptr, _ct, _sz, _align) __safe_aligned_zalloc((_ptr), (_ct), (_sz), (_align))

/**
 * Macro to help with allocating a single structure
 */
#define TZALLOC(_ptr) TCALLOC((void **)&(_ptr), (size_t)1, sizeof( *(_ptr) ))

/**
 * Macro to help with allocating a single structure, with alignment
 */
#define TZAALLOC(_ptr, _align) __safe_aligned_zalloc((void **)&(_ptr), sizeof(*(_ptr)), 1ul, (_align))

#define TFREE(_ptr) do { \
    free((void *)(_ptr)); \
    _ptr = NULL; \
} while (0)

/* CAL_CLEANUP helpers */
void free_u32_array(uint32_t **ptr);
void free_i16_array(int16_t **ptr);
void free_double_array(double **ptr);
void free_memory(void **ptr);
void free_string(char **str);

CAL_CHECKED aresult_t __safe_malloc(void **ptr, size_t size);
CAL_CHECKED aresult_t __safe_calloc(void **ptr, size_t count, size_t size);
CAL_CHECKED aresult_t __safe_realloc(void **ptr, size_t size);

CAL_CHECKED
aresult_t __safe_aligned_zalloc(void **ptr, size_t size, size_t count, size_t align);


