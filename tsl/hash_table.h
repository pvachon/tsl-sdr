#pragma once
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

#include <tsl/list.h>
#include <tsl/errors.h>

#include <stdbool.h>

struct hash_bucket;
struct hash_node;

/**
 * Function pointer to a hash function.
 */
typedef aresult_t (*hash_table_hash_func_t)(void *key, size_t key_len, uint64_t *presult);

/**
 * Function pointer used for comparing two keys, used to confirm that the hash table has exactly the right record.
 *
 * The hash node provided is the candidate node, which was filtered based on the hash result. If the hash result
 * matches exactly, the comare function is then called, with a reference to the candidate node. The function is
 * then used to check the contents of the hash node to be sure it matches perfectly. If it does, the given node
 * will be returned to the lookup caller.
 */
typedef aresult_t (*hash_table_compare_func_t)(struct hash_node *node, void *key, size_t key_len, bool *result);

/**
 * The hash table itself. Usually embedded in another structure.
 *
 * This hash table attempts two-stage lookup. The first stage compares the known hash value
 */
struct hash_table {
    /**
     * The actual array of buckets.
     */
    struct hash_bucket *buckets;

    /**
     * The number of buckets in the hash table. This is to be a power-of-2.
     */
    size_t nr_buckets;

    /**
     * The hash function to use for this hash table. This is determined based on the key type.
     */
    hash_table_hash_func_t hfunc;

    /**
     * The comparison function
     */
    hash_table_compare_func_t compare;
};

/**
 * Quick initializer for an auto hash table
 */
#define HASH_TABLE_INIT { .buckets = NULL, .nr_buckets = 0, .hfunc = NULL, .compare = NULL }

/**
 * Hash node residing at each hash table entry. Used to reference a data structure that has been
 * inserted into the hash table.
 */
struct hash_node {
    /**
     * The computed hash value, a 64-bit integer
     */
    uint64_t hash;

    /**
     * The position of this node in the bucket chain
     */
    struct list_entry h_node;
} CAL_PACKED;

static inline
aresult_t ht_node_init(struct hash_node *node)
{
    aresult_t ret = A_OK;

    if (NULL == node) return A_E_BADARGS;

    list_init(&node->h_node);
    node->hash = 0;

    return ret;
}

/**
 * Initialize an empty hash table. Creates a table with nr_buckets buckets, using the provided hash function.
 *
 * \param table Pointer to enough memory to hold a new hash table structure. Buckets are allocated out-of-line.
 * \param nr_buckets The number of buckets to create and initialize with this hash table.
 * \param hash_func The hash function. Must return a 64-bit hash that can be used to calculate bucket offsets.
 * \param compare A comparison function, the verify that a given hash_node represents the actual key value requested.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t ht_init(struct hash_table *table, size_t nr_buckets, hash_table_hash_func_t hash_func, hash_table_compare_func_t compare);

/**
 * Clean up resources allocated for a hash table.
 *
 * \param table The table to clean up. This does not release tracked data structures.
 *
 * \return A_OK on success, an error code otherwise.
 *
 * \note This function releases some memory and paves over the structure with 0's. This does not release the memory
 *       that the hash table structure itself was contained in.
 */
aresult_t ht_cleanup(struct hash_table *table);

/**
 * Insert the given entry in the hash table
 *
 * \param table The table to insert the entry into.
 * \param key The key for this value.
 * \param key_size The size of the key, in bytes.
 * \param entry The hash_node for a data structure to be inserted into the hash table
 *
 * \return A_OK on success, an error code otherwise
 */
aresult_t ht_insert(struct hash_table *table, void *key, size_t key_size, struct hash_node *entry);

/**
 * Find the appropriate hash entry, by key.
 *
 * \param table The table to search
 * \param key The key to search for
 * \param key_size The size of the key, in bytes
 * \param pfound The found hash node, if any.
 *
 * \return A_OK on success, A_E_NOTFOUND if the key could not be found, an error code otherwise.
 *
 * \note Complexity: O(1) for bucket lookup, O(k) for retrieval, where k is the number of entries in the bucket.
 */
aresult_t ht_find(struct hash_table *table, void *key, size_t key_size, struct hash_node **pfound);

/**
 * Delete the given entry from the hash table it is in.
 *
 * \param node The node to delete from the hash table.
 *
 * \return A_OK on success, an error code otherwise.
 */
aresult_t ht_delete(struct hash_node *node);

/**
 * Hash function: basic murmur hash for an arbitrary array of bytes of a length that is either
 * unknown or larger than 64-bits.
 *
 * You can call this directly, but it's more useful used alongside a hash table.
 */
aresult_t ht_murmur_hash_byte_string(void *key, size_t key_len, uint64_t *hash_val);

/**
 * Hash function: basic murmur hash for a single 64-bit word that is passed as key (i.e. key is
 * not actually a pointer).
 *
 * key_len must always be sizeof(uint64_t), i.e. 8.
 */
aresult_t ht_murmur_hash_word(void *key, size_t key_len, uint64_t *hash_val);
