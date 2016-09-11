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
#include <tsl/cal.h>

#include <ck_pr.h>

#include <stdlib.h>

/** \file refcnt.h
 * Declaration and implementation of the basic reference counting facility for
 * objects that use the TSL.
 */

/* Forward declare */
struct refcnt;

/** \typedef refcnt_destructor_t
 * Function type for holding a pointer to a destructor function to be called when
 * the reference count drops to zero.
 */
typedef void (*refcnt_destructor_t)(struct refcnt *ref);

/** \brief Reference counting structure
 * Structure that can be embedded in another structure to guard the resources
 * managed by that structure and used by its consumers to be released when the
 * reference count drops to zero.
 * \note This structure is only to be manipulated by accessor functions.
 */
struct refcnt {
    refcnt_destructor_t destructor;         /** Function to be called */
    uint32_t refcnt;                        /** Actual reference count */
};

/** \brief Initialize a structure with a first reference
 * Initialize the specified structure with a single reference.
 * \param ref Pointer to the reference counting structure
 * \param destruct Function pointer to an optional destructor
 * \return A_OK on success, an error code on failure
 * \note If no destructor is specified, the owner must manage the lifecycle directly
 */
static inline
aresult_t refcnt_init(struct refcnt *ref, refcnt_destructor_t destruct)
{
    TSL_ASSERT_ARG(ref != NULL);

    ref->refcnt = 1;
    ref->destructor = destruct;

    return A_OK;
}

/** \brief Get a reference to a structure
 * Increment the reference count for a structure by 1, ensuring that the reference you
 * have taken to the structure is safe. If the referenced object's count is 0, the get
 * fails with an invalid pointer error.
 * \param ref Pointer to the reference counting structure
 */
static inline
aresult_t refcnt_get(struct refcnt *ref)
{
    TSL_ASSERT_ARG(ref != NULL);

    if (CAL_UNLIKELY(ref->refcnt == 0)) {
        return A_E_INVAL;
    }

    uint32_t initial, targ;     /* Initial value, target value */
    uint32_t val = 0;           /* Returned from CAS */
    bool result = 0;            /* Result of CAS */
    do {
        initial = ref->refcnt;
        targ = initial + 1;

        /* Compare and swap the value */
        result = ck_pr_cas_32_value(&ref->refcnt, initial, targ, &val);

        if (val == 0) {
            break;
        }
    } while (result == false);

    /* In the unlikely case that while we were doing that mess, the last reference
     * was dropped, die.
     */
    if (CAL_UNLIKELY(val == 0)) {
        return A_E_INVAL;
    }

    /* We got the reference, so we're good to go. */
    return A_OK;
}

/** \brief Release a reference to a structure
 * Decrement the reference count for a structure by 1. If the reference count reaches
 * 0, the object is considered dead. Generates an argument assertion if the reference
 * count is incorrect. If the object's reference count is 0, the destructor function,
 * if specified during initialization, will be called.
 * \param ref Pointer to the reference counting structure
 */
static inline
aresult_t refcnt_release(struct refcnt *ref)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(ref);

    if (CAL_UNLIKELY(ref->refcnt == 0)) {
        ret = A_E_INVAL;
        goto done;
    }

    /* Don't need to do this atomically, thankfully */
    ref->refcnt--;

    if (CAL_UNLIKELY(ref->refcnt == 0)) {
        /* Call the destructor, if specified */
        if (ref->destructor) {
            ref->destructor(ref);
        }
    }

done:
    return ret;
}

/** \brief Determine if an object is dead
 * If a given reference counted object has a reference count of 0, indicate the object
 * is dead, and should be reaped.
 */
static inline
aresult_t refcnt_check(struct refcnt *ref, int *valid)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(ref != NULL);
    TSL_ASSERT_ARG(valid != NULL);

    *valid = (ref->refcnt != 0);

    return ret;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
