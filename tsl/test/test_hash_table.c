#include <tsl/test/helpers.h>

#include <tsl/hash_table.h>
#include <tsl/basic.h>
#include <tsl/assert.h>

struct test_ht_entry {
    uint64_t id;
    struct hash_node h_node;
};

static
aresult_t _test_compare_ht_entry(struct hash_node *hnode, void *key, size_t key_len, bool *result)
{
    aresult_t ret = A_OK;

    struct test_ht_entry *entry = NULL;

    TSL_ASSERT_ARG(NULL != hnode);
    TSL_ASSERT_ARG(8 == key_len);
    TSL_ASSERT_ARG(NULL != result);

    entry = BL_CONTAINER_OF(hnode, struct test_ht_entry, h_node);

    *result = (intptr_t)key == entry->id;

    return ret;
}

TEST_DECL(test_hash_table_basic)
{
    struct hash_table test_table = HASH_TABLE_INIT;

    struct test_ht_entry entries[4096];

    TEST_ASSERT_OK(ht_init(&test_table, 2048, ht_murmur_hash_word, _test_compare_ht_entry));

    for (size_t i = 0; i < BL_ARRAY_ENTRIES(entries); i++) {
        struct test_ht_entry *ent = &entries[i];

        ent->id = i;
        TEST_ASSERT_OK(ht_insert(&test_table, (void *)i, sizeof(uint64_t), &ent->h_node));
    }

    for (size_t i = 0; i < BL_ARRAY_ENTRIES(entries); i++) {
        struct hash_node *hnode  = NULL;
        struct test_ht_entry *ent = NULL;

        TEST_ASSERT_OK(ht_find(&test_table, (void *)i, sizeof(uint64_t), &hnode));

        ent = BL_CONTAINER_OF(hnode, struct test_ht_entry, h_node);

        TEST_ASSERT_EQUALS(ent->id, i);
        TEST_ASSERT_EQUALS(ent, &entries[i]);
    }

    for (size_t i = 0; i < BL_ARRAY_ENTRIES(entries); i += 2) {
        struct hash_node *hnode  = NULL;
        struct test_ht_entry *ent = NULL;
        TEST_ASSERT_OK(ht_find(&test_table, (void *)i, sizeof(uint64_t), &hnode));
        ent = BL_CONTAINER_OF(hnode, struct test_ht_entry, h_node);

        TEST_ASSERT_EQUALS(ent->id, i);
        TEST_ASSERT_EQUALS(ent, &entries[i]);

        TEST_ASSERT_OK(ht_delete(hnode));

        TEST_ASSERT_EQUALS(ht_find(&test_table, (void *)i, sizeof(uint64_t), &hnode), A_E_NOTFOUND);
    }

    TEST_ASSERT_OK(ht_cleanup(&test_table));

    return TEST_OK;
}
