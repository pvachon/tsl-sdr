#include <tsl/test/helpers.h>
#include <tsl/alloc.h>
#include <tsl/alloc/alloc_priv.h>
#include <tsl/list.h>

TEST_DECL(test_alloc_basic)
{
    struct allocator *alloc = NULL;

    /* This test can use normal pages */
    TEST_ASSERT_OK(allocator_system_init(4096, 4096, 0, 0));

    /* Test 1: create a new allocator */
    TEST_ASSERT_OK(allocator_new(&alloc, 42, 128, 0));

    TEST_ASSERT_EQUALS(alloc->item_size, 48);
    TEST_ASSERT_EQUALS(alloc->max_items, 168);
    TEST_ASSERT_EQUALS(alloc->free_mask, (~((size_t)4096 - 1)));

    /* Test 2: destroy the new allocator */
    TEST_ASSERT_EQUALS(allocator_delete(&alloc), A_OK);

    TEST_ASSERT_EQUALS(alloc, NULL);

    /* Test 3: create a new allocator, allocate items */
    TEST_ASSERT_EQUALS(allocator_new(&alloc, 42, 128, 0), A_OK);

    void *items[32];
    for (size_t i = 0; i < sizeof(items)/sizeof(void*); ++i) {
        void *item = NULL;
        TEST_ASSERT_EQUALS(allocator_alloc(alloc, &item), A_OK);
        TEST_ASSERT_NOT_EQUALS(item, NULL);
        items[i] = item;
    }

    /* Test 4: return almost all items to the allocator in reverse order */
    for (size_t i = 0; i < (sizeof(items)/sizeof(void*)) - 1; ++i) {
        void *item = items[32 - 1 - i];
        TEST_ASSERT_NOT_EQUALS(item, NULL);
        TEST_ASSERT_EQUALS(allocator_free(alloc, &item), A_OK);
        TEST_ASSERT_EQUALS(item, NULL);
    }

    /* Test 5: attempt to free allocator with one outstanding item */
    TEST_ASSERT_EQUALS(allocator_delete(&alloc), A_E_BUSY);

    /* Test 6: release the remaining item */
    void *item_6 = items[0];
    TEST_ASSERT_EQUALS(allocator_free(alloc, &item_6), A_OK);

    /* Test 7: actually release the allocator */
    TEST_ASSERT_EQUALS(allocator_delete(&alloc), A_OK);

    /* Test 8: Create a new allocator that should grow */
    TEST_ASSERT_EQUALS(allocator_new(&alloc, 64, 128, 0), A_OK);
    TEST_ASSERT_NOT_EQUALS(alloc, NULL);

    size_t total_items = alloc->max_items;
    struct list_entry head;
    list_init(&head);

    for (size_t i = 0; i < total_items; i++) {
        struct list_entry *item = NULL;
        TEST_ASSERT_EQUALS(allocator_alloc(alloc, (void **)&item), A_OK);
        list_append(&head, item);
    }

    for (size_t i = 0; i < 128; i++) {
        struct list_entry *item = NULL;
        TEST_ASSERT_EQUALS(allocator_alloc(alloc, (void **)&item), A_OK);
        list_append(&head, item);
    }

    struct list_entry *iter, *temp;
    list_for_each_safe(iter, temp, &head) {
        if (iter == &head) {
            printf("Broken.\n");
        }
        list_del(iter);
        TEST_ASSERT_EQUALS(allocator_free(alloc, (void **)&iter), A_OK);
        TEST_ASSERT_EQUALS(iter, NULL);
    }

    TEST_ASSERT_EQUALS(allocator_delete(&alloc), A_OK);
    TEST_ASSERT_EQUALS(alloc, NULL);

    return TEST_OK;
}
