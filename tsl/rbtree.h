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

#include <tsl/result.h>
#include <tsl/assert.h>

/** \file rbtree.h
 * Declaration of associated structures and functions for a simple, intrusive
 * red-black tree implementation.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** \defgroup rb_tree_state State Structures
 * Structures that are used to represent state of a red-black tree, including the
 * state of the tree itself, comparison functions used to determine how the tree
 * is to be traversed, and representations of red-black tree nodes themselves.
 * @{
 */

/**
 * Structure that represents a node in a red-black tree. Embed this in your own
 * structure in order to add your structure to the given red-black tree.
 * Users of the rb_tree_node would embed it something like
 * \code{.c}
    struct my_sample_struct {
        char *name;
        int data;
        struct rb_tree_node rnode;
    };
 * \endcode
 *
 * \note No user of `struct rb_tree_node` should ever modify or inspect any
 *       members of the structure.
 */
struct rb_tree_node {
    /**
     * The left child (`NULL` if empty)
     */
    struct rb_tree_node *left;

    /**
     * The right child (`NULL` if empty)
     */
    struct rb_tree_node *right;

    /**
     * The parent of this node (`NULL` if at root)
     */
    struct rb_tree_node *parent;

    /**
     * The key for this node
     */
    void *key;

    /**
     * The color of the node
     */
    int color;
};

/**
 * Pointer to a comparison function that allows passing along state.
 * Return values are interpreted as follows:
 *  (0, +inf] if lhs > rhs
 *  0 if lhs == rhs
 *  [-inf, 0) if lhs < rhs
 */
typedef int (*rb_cmp_func_ex_t)(void *state, void *lhs, void *rhs);

/**
 * Pointer to a function to compare two keys, and returns as follows:
 *  (0, +inf] if lhs > rhs
 *  0 if lhs == rhs
 *  [-inf, 0) if lhs < rhs
 */
typedef int (*rb_cmp_func_t)(void *lhs, void *rhs);

/**
 * Structure representing an RB tree's associated state. Contains all
 * the information needed to manage the lifecycle of a RB tree.
 * \note Typically users should not directly manipulate the structure,
 *       but rather use the provided accessor functions.
 */
struct rb_tree {
    /**
     * The root of the tree
     */
    struct rb_tree_node *root;

    /**
     * Predicate used for traversing the tree
     */
    rb_cmp_func_ex_t compare;

    /**
     * The left-most node of the rb-tree
     */
    struct rb_tree_node *rightmost;

    /**
     * Private state that can be used by the rb-tree owner
     */
    void *state;
};

/**@} rb_tree_state */

/** \defgroup rb_functions Functions for Manipulating Red-Black Trees
 * All functions associated with manipulating Red-Black trees using `struct rb_tree`,
 * inluding lifecycle functions and member manipulation and state checking functions.
 * @{
 */
/**
 * \brief Construct a new, empty red-black tree, with extended state
 * Given a region of memory at least the size of a struct rb_tree to
 * store the red-black tree metadata, update it to contain an initialized, empty
 * red-black tree, with given private state.
 * \param tree Pointer to the new tree.
 * \param compare Function used to traverse the tree.
 * \param state The private state to be passed to the compare function
 * \return RB_OK on success, an error code otherwise
 */
aresult_t rb_tree_new_ex(struct rb_tree *tree, rb_cmp_func_ex_t compare, void *state);

/**
 * \brief Construct a new, empty red-black tree
 * Given a region of memory at least the size of a struct rb_tree to
 * store the red-black tree metadata, update it to contain an initialized, empty
 * red-black tree.
 * \param tree Pointer to the new tree.
 * \param compare Function used to traverse the tree.
 * \return RB_OK on success, an error code otherwise
 */
aresult_t rb_tree_new(struct rb_tree *tree,
                      rb_cmp_func_t compare);

/**
 * \brief Destroy an RB-tree
 * Erase an RB-tree.
 * \param tree The reference to the pointer to the tree itself.
 * \return RB_OK on success, an error code otherwise
 */
aresult_t rb_tree_destroy(struct rb_tree *tree);

/**
 * \brief Check if an RB-tree is empty (has no nodes)
 * \param tree The tree to check
 * \param is_empty nonzero on true, 0 otherwise
 * \return RB_OK on success, an error code otherwise
 */
aresult_t rb_tree_empty(struct rb_tree *tree, int *is_empty);

/**
 * \brief Find a node in the RB-tree given the specified key.
 * Given a key, search the RB-tree iteratively until the specified key is found.
 * \param tree The RB-tree to search
 * \param key The key to search for
 * \param value a reference to a pointer to receive the pointer to the rb_tree_node if key is found
 * \return RB_OK on success, an error code otherwise
 */
aresult_t rb_tree_find(struct rb_tree *tree,
                       void *key,
                       struct rb_tree_node **value);

/**
 * \brief Insert a node into the tree
 * Given a node with a populated key, insert the node into the RB-tree
 * \param tree the RB tree to insert the node into
 * \param key The key for the node (must live as long as the node itself is in the tree)
 * \param node the node to be inserted into the tree
 * \return RB_OK on sucess, an error code otherwise
 */
aresult_t rb_tree_insert(struct rb_tree *tree,
                         void *key,
                         struct rb_tree_node *node);

/**
 * \brief Remove the specified node from the rb_tree
 * Removes a specified node from the red-black tree.
 * \param tree The tree we want to remove the node from
 * \param node The the node we want to remove
 * \return RB_OK on success, an error code otherwise
 */
aresult_t rb_tree_remove(struct rb_tree *tree,
                         struct rb_tree_node *node);

/**
 * \brief Find a node. If not found, insert the candidate.
 * Find a node with the given key. If the node is found, return it by
 * reference, without modifying the tree. If the node is not found,
 * insert the provided candidate node.
 * \note This function always will return in *value the node inserted
 *       or the existing node. If you want to check if the candidate
 *       node was inserted, check if `*value == new_candidate`
 *
 * \param tree The tree in question
 * \param key The key to search for
 * \param new_candidate The candidate node to insert
 * \param value The value at the given location
 * \return A_OK on success, an error code otherwise
 */
aresult_t rb_tree_find_or_insert(struct rb_tree *tree,
                                 void *key,
                                 struct rb_tree_node *new_candidate,
                                 struct rb_tree_node **value);

/**
 * \brief Get the rightmost (greatest relative to predicate) node.
 * Return the rightmost (i.e. greatest relative to predicate) node of the Red-Black tree.
 */
static inline
aresult_t rb_tree_get_rightmost(struct rb_tree *tree,
                                struct rb_tree_node **rightmost)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(tree != NULL);
    TSL_ASSERT_ARG_DEBUG(rightmost != NULL);

    *rightmost = tree->rightmost;

    return ret;
}

/**
 * Find the minimum of the given tree/subtree rooted at the given node.
 */
static inline
aresult_t __rb_tree_find_minimum(struct rb_tree_node *root,
                                 struct rb_tree_node **min)
{
    struct rb_tree_node *x = root;

    while (x->left != NULL) {
        x = x->left;
    }

    *min = x;

    return A_OK;
}

/**
 * Find the maximum of the given tree/subtree rooted at the given node.
 */
static inline
aresult_t __rb_tree_find_maximum(struct rb_tree_node *root,
                                 struct rb_tree_node **max)
{
    struct rb_tree_node *x = root;

    while (x->right != NULL) {
        x = x->right;
    }

    *max = x;

    return A_OK;
}

/**
 * Find the successor (greater than, relative to predicate) node of the given node.
 */
static inline
aresult_t rb_tree_find_successor(struct rb_tree *tree,
                                 struct rb_tree_node *node,
                                 struct rb_tree_node **successor)
{
    aresult_t ret = A_OK;

    TSL_ASSERT_ARG_DEBUG(tree != NULL);
    TSL_ASSERT_ARG_DEBUG(node != NULL);
    TSL_ASSERT_ARG_DEBUG(successor != NULL);

    struct rb_tree_node *x = node;

    if (x->right != NULL) {
        __rb_tree_find_minimum(x->right, successor);
        goto done;
    }

    struct rb_tree_node *y = x->parent;

    while (y != NULL && (x == y->right)) {
        x = y;
        y = y->parent;
    }

    *successor = y;

done:
    return ret;
}

/**
 * Find the predecessor (less than, relative to predicate) node of the given node.
 */
static inline
aresult_t rb_tree_find_predecessor(struct rb_tree *tree,
                                   struct rb_tree_node *node,
                                   struct rb_tree_node **pred)
{
    aresult_t ret = A_OK;
    struct rb_tree_node *x = node;

    TSL_ASSERT_ARG_DEBUG(tree != NULL);
    TSL_ASSERT_ARG_DEBUG(node != NULL);
    TSL_ASSERT_ARG_DEBUG(pred != NULL);

    if (x->left != NULL) {
        __rb_tree_find_maximum(x->left, pred);
        goto done;
    }

    struct rb_tree_node *y = x->parent;

    while (y != NULL && (x == y->left)) {
        x = y;
        y = y->parent;
    }

    *pred = y;

done:
    return ret;
}

/**@} rb_functions */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */
