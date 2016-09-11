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
#include <tsl/list.h>
#include <tsl/errors.h>
#include <tsl/diag.h>

struct notifier_chain {
    /* Private. List of listeners */
    struct list_item listeners;
};

struct notifier_block {
    /* The notification function */
    void (*listener_notifier)(struct notifier_chain *, struct notifier_block, void *);

    /* Private data for the listener function to use for state */
    void *priv;

    /* Linked list node */
    struct list_item lnode;
};

static inline
aresult_t notifier_init(struct notifier_chain *chain)
{
    TSL_ASSERT_ARG(chain != NULL);
    list_init_item(&chain->listeners);
    return A_OK;
}

static inline
aresult_t notifier_destroy(struct notifier_chain *chain)
{
    aresult_t ret = A_OK;
    TSL_ASSERT_ARG(chain != NULL);

    /* Unlink the entire notifier list */
    struct notifier_block *curs, *temp;
    list_for_each_type_safe(curs, temp, &chain->listeners, lnode) {
        list_del(&curs->lnode);
    }

    return ret;
}

static inline
aresult_t notifier_add_listener(struct notifier_chain *chain,
                                struct notifier_block *block)
{
    aresult_t ret = A_OK;
    TSL_ASSERT_ARG(chain != NULL);
    TSL_ASSERT_ARG(block != NULL);

    if (block->listener_notifier == NULL) {
        ret = A_E_INVAL;
        goto done;
    }

    list_append(&chain->listeners, &block->lnode);

done:
    return ret;
}

static inline
aresult_t notifier_remove_listener(struct notifier_chain *chain,
                                   struct notifier_block *block)
{
    aresult_t ret = A_OK;
    TSL_ASSERT_ARG(chain != NULL);
    TSL_ASSERT_ARG(block != NULL);

    list_del(&block->lnode);

    return ret;
}

static inline
aresult_t notifier_notify(struct notifier_chain *chain,
                          void *message)
{
    aresult_t ret = A_OK;
    TSL_ASSERT_ARG(chain != NULL);

    struct notifier_block *block;

    /* Notify each listener of what is going on */
    list_for_each_type(block, &chain->listeners, lnode) {
        if (block->listener_notifier) {
            block->listener_notifier(chain, block, message);
        }
    }

    return ret;
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
