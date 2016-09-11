#include <tsl/test/helpers.h>
#include <tsl/rbtree.h>
#include <tsl/assert.h>
#include <tsl/basic.h>
#include <tsl/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static
int test_rbtree_compare(void *lhs, void *rhs)
{
    int64_t n_lhs = (int64_t)lhs;
    int64_t n_rhs = (int64_t)rhs;

    return (int)(n_lhs - n_rhs);
}

#ifdef _TEST_RBTREE_DUMP_TREE
static
void test_rbtree_print(struct rb_tree_node *node)
{
    int64_t val = (int64_t)(node->key);
    printf("%d", (int)val);
}
#endif

struct test_rbtree_node {
    struct rb_tree_node node;
    int test;
};

#define COLOR_BLACK         0x0
#define COLOR_RED           0x1
/* #define _TEST_RBTREE_DUMP_TREE */
/* #define _TEST_RBTREE_PRINTING */

#ifdef _TEST_RBTREE_DUMP_TREE
static
void dump_rb_tree(struct test_rbtree_node *nodes, size_t num_nodes)
{
    printf("digraph TreeDump {\n");
    for (size_t i = 0; i < num_nodes; ++i) {
        struct rb_tree_node *node = &(nodes[i].node);
        struct rb_tree_node *left = node->left;
        struct rb_tree_node *right = node->right;

        if (node->parent == NULL && node->left == NULL && node->right == NULL) {
            test_rbtree_print(node);
            printf("[color=blue, style=filled];\n");
            continue;
        }

        test_rbtree_print(node);
        printf("[color=%s, style=dotted];\n",
                node->color == COLOR_RED ? "red" : "black");

        test_rbtree_print(node);
        printf(" -> ");
        if (left) {
            test_rbtree_print(left);
        } else {
            printf("nil");
        }
        printf("[label=left];\n");

        test_rbtree_print(node);
        printf(" -> ");
        if (right) {
            test_rbtree_print(right);
        } else {
            printf("nil");
        }
        printf("[label=right];\n");

    }
    printf("}\n");
}
#endif

static
int assert_rbtree(struct rb_tree *my_tree, struct test_rbtree_node *nodes,
                  size_t node_count)
{
    int prev_black_height = 0;

    for (size_t i = 0; i < node_count; ++i) {

        struct rb_tree_node *node = &(nodes[i].node);
        struct rb_tree_node *parent = node->parent;
        struct rb_tree_node *left = node->left;
        struct rb_tree_node *right = node->right;
        struct rb_tree_node *tmp_node = &(nodes[i].node);
        int height = 0;
        int black_height = 0;

        if (parent == NULL && left == NULL && right == NULL) {
            continue;
        }

        if (parent == NULL) {
            TEST_ASSERT_EQUALS(node->color, COLOR_BLACK);
        }
        if (node->color == COLOR_RED) {
            TEST_ASSERT((!left || left->color == COLOR_BLACK) && (!right || right->color == COLOR_BLACK));
        } else {
            TEST_ASSERT_EQUALS(node->color, COLOR_BLACK);
        }

        if (left == NULL || right == NULL) {
            while (tmp_node != NULL) {
                height++;
                if (tmp_node->color == COLOR_BLACK) {
                    black_height++;
                }
                tmp_node = tmp_node->parent;
            }
            TEST_ASSERT((prev_black_height == 0) || (black_height == prev_black_height));
#ifdef _TEST_RBTREE_PRINTING
            printf("\nblack_height: %d prev: %d", black_height, prev_black_height);
#endif
            prev_black_height = black_height;
        }

    }

    return TEST_OK;
}

static
int traverse_rbtree(struct rb_tree *my_tree)
{
#ifdef _TEST_RBTREE_PRINTING
    printf("\nin-order traverse on rb-tree: \n{ ");
#endif
    struct rb_tree_node *node = my_tree->root;
    struct rb_tree_node *prev = NULL;
    void* prev_key = NULL;

    while (node != NULL) {
        if (prev == node->parent)
        {
            if (node->left != NULL) {
                prev = node;
                node = node->left;
                continue;
            } else {
                prev = NULL;
            }
        }
        if (prev == node->left)
        {
            if (prev_key != NULL) {
                TEST_ASSERT(my_tree->compare(my_tree->state, prev_key, node->key) < 0);
            }
#ifdef _TEST_RBTREE_PRINTING
            printf("%zu ", (size_t)node->key);
#endif
            prev_key = node->key;

            if (node->right != NULL) {
                prev = node;
                node = node->right;
                continue;
            } else {
                prev = NULL;
            }
        }
        if (prev == node->right) {
            prev = node;
            node = node->parent;
        }
    }
#ifdef _TEST_RBTREE_PRINTING
    printf("}\n");
#endif

    return TEST_OK;
}

/* Lifecycle Test (Functions, etc.):
 * 1. New (rb_tree_new)
 * 2. Destroy (rb_tree_destroy) on empty tree
 * 3. Insertion (rb_tree_insert)
 * 4. Deletion (rb_tree_remove)
 * 5. Color-scheme (assert_rbtree)
 * 6. Print (dump_rb_tree)
 * 7. Traverse and sorting (traverse_rbtree)
 * 7.1 Traverse another way (rb_tree_traverse)
 * 8. Find (rb_tree_find)
 * 9. Destroy (rb_tree_destroy) on non-empty tree
 * 10. Height/black_height (assert_rbtree)
 */
TEST_DECL(test_rbtree_lifecycle)
{
    struct rb_tree my_tree;

    TEST_ASSERT_EQUALS(rb_tree_new(&my_tree, test_rbtree_compare), A_OK);
    TEST_ASSERT_EQUALS(rb_tree_destroy(&my_tree), A_OK);

    TEST_ASSERT_EQUALS(rb_tree_new(&my_tree, test_rbtree_compare), A_OK);

    struct test_rbtree_node nodes[30];

    memset(nodes, 0, sizeof(nodes));

    for (size_t i = 0; i < BL_ARRAY_ENTRIES(nodes); ++i) {
        void *key = (void*)((int64_t)(i) + (i % 2 ? 42 : -42));
        TEST_ASSERT_EQUALS(rb_tree_insert(&my_tree, key, &(nodes[i].node)), A_OK);
    }


    if (assert_rbtree(&my_tree, nodes, BL_ARRAY_ENTRIES(nodes)) != TEST_OK) {
#ifdef _TEST_RBTREE_DUMP_TREE
        dump_rb_tree(nodes, BL_ARRAY_ENTRIES(nodes));
#endif
        int assert_rbtree_failed = 0;
        TEST_ASSERT_EQUALS(assert_rbtree_failed, 1);
    }
    traverse_rbtree(&my_tree);

    struct treestate {
      int iterations;
      int max_iterations;
    };

    struct rb_tree_node* found_node;
    void *key_available = (void*)((int64_t)(5) + (5 % 2 ? 42 : -42));
    TEST_ASSERT_EQUALS(rb_tree_find(&my_tree, key_available, &found_node), A_OK);
    TEST_ASSERT((&my_tree)->compare(my_tree.state, found_node->key, key_available) == 0);

    void *key_not_available = (void*)((int64_t)(23) + (35 % 2 ? 42 : -42)) + 1;
    TEST_ASSERT_NOT_EQUALS(rb_tree_find(&my_tree, key_not_available, &found_node), A_OK);
    TEST_ASSERT_EQUALS(found_node, NULL);

#if 0
    void *key_to_remove = (void *)-42;
    TEST_ASSERT_EQUALS(rb_tree_find(&my_tree, key_to_remove, &found_node), A_OK);
    TEST_ASSERT_NOT_EQUALS(found_node, NULL);
    TEST_ASSERT_EQUALS(rb_tree_remove(&my_tree, found_node), A_OK);

#endif

#if 0
    for (size_t i = 0; i < BL_ARRAY_ENTRIES(nodes); i++) {
        if ((int64_t)nodes[i].node.key != -42) {
            continue;
        }

        TEST_ASSERT_EQUALS(rb_tree_remove(&my_tree, &nodes[i].node), A_OK);
        break;
    }

    dump_rb_tree(nodes, BL_ARRAY_ENTRIES(nodes));
#endif

    for (size_t i = 0; i < BL_ARRAY_ENTRIES(nodes); i+=3) {
        TEST_ASSERT_EQUALS(rb_tree_remove(&my_tree, &nodes[i].node), A_OK);

        if (assert_rbtree(&my_tree, nodes, BL_ARRAY_ENTRIES(nodes)) != TEST_OK) {
            int assert_rbtree_failed = 0;
#ifdef _TEST_RBTREE_DUMP_TREE
            dump_rb_tree(nodes, BL_ARRAY_ENTRIES(nodes));
#endif
            TEST_ASSERT_EQUALS(assert_rbtree_failed, 1);
        }
    }

    TEST_ASSERT_EQUALS(rb_tree_destroy(&my_tree), A_OK);

    return TEST_OK;
}

TEST_DECL(test_rbtree_corner_cases)
{
    struct rb_tree my_tree;

    TEST_ASSERT_EQUALS(rb_tree_new(&my_tree, test_rbtree_compare), A_OK);

    struct test_rbtree_node single_node;
    memset(&single_node, 0, sizeof(single_node));
    single_node.node.key = (void*)(uint64_t)128;
    TEST_ASSERT_EQUALS(rb_tree_insert(&my_tree, &single_node.node.key, &single_node.node), A_OK);

    TEST_ASSERT_EQUALS(my_tree.root->color, COLOR_BLACK);

    TEST_ASSERT_EQUALS(rb_tree_remove(&my_tree, &single_node.node), A_OK);
    TEST_ASSERT_EQUALS(rb_tree_destroy(&my_tree), A_OK);

    return TEST_OK;
}
