/*
  Copyright (c) 2014, 12Sided Technology, LLC
  Author: Phil Vachon <pvachon@12sidedtech.com>
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
#include <tsl/hash_table.h>

#include <tsl/assert.h>
#include <tsl/diag.h>
#include <tsl/errors.h>
#include <tsl/list.h>
#include <tsl/safe_alloc.h>

#include <stdlib.h>
#include <string.h>

/**
 * Structure that resides at each hash table entry to enable chaining
 */
struct hash_bucket {
    /**
     * The list of entries in this hash bucket.
     */
    struct list_entry entries;
} CAL_PACKED;

aresult_t ht_init(struct hash_table *table, size_t nr_buckets, hash_table_hash_func_t hash_func, hash_table_compare_func_t compare)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != table);
    TSL_ASSERT_ARG(0 != nr_buckets);
    TSL_ASSERT_ARG(NULL != hash_func);
    TSL_ASSERT_ARG(NULL != compare);

    if (NULL != table->buckets) {
        ret = A_E_BUSY;
        goto done;
    }

    table->buckets = calloc(nr_buckets, sizeof(struct hash_bucket));
    if (NULL == table->buckets) {
        DIAG("Failed to allocate memory for hash table buckets, aborting.");
        ret = A_E_INVAL;
        goto done;
    }

    for (size_t i = 0; i < nr_buckets; i++) {
        struct hash_bucket *bkt = &table->buckets[i];
        /* Initialize an empty collision list */
        list_init(&bkt->entries);
    }

    table->nr_buckets = nr_buckets;
    table->hfunc = hash_func;
    table->compare = compare;

done:
    return ret;
}

aresult_t ht_cleanup(struct hash_table *table)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != table);
    TSL_ASSERT_ARG(NULL != table->buckets);

    TFREE(table->buckets);
    memset(table, 0, sizeof(*table));

    return ret;
}

static
aresult_t _ht_get_bucket(struct hash_table *table, void *key, size_t key_size, struct hash_bucket **bkt, uint64_t *hash)
{
    aresult_t ret = A_OK;

    uint64_t hash_val = 0, offset = 0;

    TSL_ASSERT_ARG_DEBUG(NULL != table);
    TSL_ASSERT_ARG_DEBUG(0 != key_size);
    TSL_ASSERT_ARG_DEBUG(NULL != bkt);
    TSL_ASSERT_ARG_DEBUG(NULL != hash);

    *bkt = NULL;

    if (AFAILED(ret = table->hfunc(key, key_size, &hash_val))) {
        DIAG("Failure while generating key hash");
        goto done;
    }

    offset = hash_val & (table->nr_buckets - 1);

    assert(offset < table->nr_buckets);

    *bkt = &table->buckets[offset];

    *hash = hash_val;

done:
    return ret;
}

aresult_t ht_insert(struct hash_table *table, void *key, size_t key_size, struct hash_node *entry)
{
    aresult_t ret = A_OK;

    struct hash_bucket *bkt = NULL;
    uint64_t hash_val = 0;

    TSL_ASSERT_ARG(NULL != table);
    /* Note: key can be 0, but key size CANNOT be 0 */
    TSL_ASSERT_ARG(0 != key_size);
    TSL_ASSERT_ARG(NULL != entry);

    if (AFAILED_UNLIKELY(ret = _ht_get_bucket(table, key, key_size, &bkt, &hash_val))) {
        goto done;
    }

    entry->hash = hash_val;
    list_append(&bkt->entries, &entry->h_node);

done:
    return ret;
}

aresult_t ht_find(struct hash_table *table, void *key, size_t key_size, struct hash_node **pfound)
{
    aresult_t ret = A_OK;

    struct hash_bucket *bkt = NULL;
    uint64_t hash_val = 0;
    struct hash_node *hnode = NULL;

    TSL_ASSERT_ARG(NULL != table);
    TSL_ASSERT_ARG(0 != key_size);
    TSL_ASSERT_ARG(NULL != pfound);

    *pfound = NULL;

    if (AFAILED_UNLIKELY(ret = _ht_get_bucket(table, key, key_size, &bkt, &hash_val))) {
        goto done;
    }

    list_for_each_type(hnode, &bkt->entries, h_node) {
        if (hash_val == hnode->hash) {
            bool res = false;
            if (AFAILED_UNLIKELY(ret = table->compare(hnode, key, key_size, &res))) {
                break;
            }

            if (true == res) {
                *pfound = hnode;
                goto done;
            }
        }
    }

    ret = A_E_NOTFOUND;

done:
    return ret;
}

aresult_t ht_delete(struct hash_node *node)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG(NULL != node);

    list_del(&node->h_node);
    node->hash = 0;

    return ret;
}

aresult_t ht_murmur_hash_byte_string(void *key, size_t key_len, uint64_t *hash_val)
{
    aresult_t ret = A_OK;

    const uint64_t m = 0xc6a4a7935bd1e995ull;
    const int r = 47;
    const uint64_t seed = 0xdeadbeef;
    uint64_t h;

    TSL_ASSERT_ARG_DEBUG(NULL != key);
    TSL_ASSERT_ARG_DEBUG(0 != key_len);
    TSL_ASSERT_ARG_DEBUG(NULL != hash_val);

    h = seed ^ (key_len * m);

    const uint64_t *data = (const uint64_t *)key;
    const uint64_t *end = data + (key_len/8);

    while (data != end) {
        uint64_t k;

        if (!((uintptr_t)data & 0x7)) {
            k = *data++;
        } else {
            memcpy(&k, (void *)data, sizeof(k));
            data++;
        }

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char * data2 = (const unsigned char*)data;

    switch (key_len & 7) {
        case 7: h ^= (uint64_t)(data2[6]) << 48;
        case 6: h ^= (uint64_t)(data2[5]) << 40;
        case 5: h ^= (uint64_t)(data2[4]) << 32;
        case 4: h ^= (uint64_t)(data2[3]) << 24;
        case 3: h ^= (uint64_t)(data2[2]) << 16;
        case 2: h ^= (uint64_t)(data2[1]) << 8;
        case 1: h ^= (uint64_t)(data2[0]);
                h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    *hash_val = h;

    return ret;
}

aresult_t ht_murmur_hash_word(void *key, size_t key_len, uint64_t *hash_val)
{
    aresult_t ret = A_OK;

    const uint64_t m = 0xc6a4a7935bd1e995ull;
    const size_t r = 47;
    const uint64_t seed = 0xbebafeca;
    uint64_t h, k;

    TSL_ASSERT_ARG_DEBUG(8 == key_len);
    TSL_ASSERT_ARG_DEBUG(NULL != hash_val);

    h = seed ^ (8 * m);

    k = (uint64_t)key * m;

    k ^= k >> r;
    k *= m;

    h ^= k;
    h *= m;

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    *hash_val = h;

    return ret;
}
