#pragma once
/*
 *  list.h - Intrusive linked list and iterators
 *
 *  Copyright (c)2017 Phil Vachon <phil@security-embedded.com>
 *
 *  This file is a part of The Standard Library (TSL)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


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

/**
 * The Standard Library Generic Linked Lists
 */

#include <tsl/version.h>
#include <tsl/basic.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define LIST_POISON_NEXT    0xc1c1c1c1
#define LIST_POISON_PREV    0x81818181

/**
 * \struct list_entry
 * Structure that represents a linked list node or a linked list head.
 */
struct list_entry {
    struct list_entry *prev;
    struct list_entry *next;
};

/** \brief Initialize a static list item
 * Initialize a static list item to an empty state in .data
 */
#define LIST_INIT(item) { .prev = &(item), .next = &(item) }

/** \brief Declare a new list head
 */
#define LIST_HEAD(name) struct list_entry name = LIST_INIT(name)

/**
 * \brief Get pointer to containing struct
 * If a list_entry is embedded in another struct, return a pointer to the
 * containing struct.
 */
#define LIST_ITEM(list_ptr, type, member) \
    BL_CONTAINER_OF(list_ptr, type, member)

/**
 * Get the next item from a list
 */
#define LIST_NEXT(list_ptr) ((list_ptr)->next)

/**
 * Get the previous item from a list
 */
#define LIST_PREV(list_ptr) ((list_ptr)->prev)

/**
 * Get the next item of the list as the given type. MUST check if list is empty before using.
 */
#define LIST_NEXT_TYPE(ptr, type, memb) LIST_ITEM(LIST_NEXT(ptr), type, memb)

/**
 * Get the previous item of the list as the given type. MUST check if list is empty before using.
 */
#define LIST_PREV_TYPE(ptr, type, memb) LIST_ITEM(LIST_PREV(ptr), type, memb)

/**
 * \brief Iterate through all items in a linked list
 * Iterate through a linked list and populate the provided iterator
 * with the current active list item. Behaves like a generic for
 * loop.
 * \param iterator a struct list_entry pointer for iteration
 * \param head the list head
 * \note Use this macro with care -- it doesn't support assignment
 *       and can unintentionally fuse with other code blocks
 */
#define list_for_each(iterator, head)           \
    for ((iterator) = (head)->next;             \
            ((iterator) != (head));             \
            (iterator) = (iterator)->next)

/**
 * \brief Iterate through all items in a linked list, by type
 * Iterate through a linked list and populate the provided iterator,
 * a pointer of the specified container type, with a pointer to
 * the item in the list.
 * \param iterator an iterator of the type in which the struct list is embedded
 * \param head the struct list_head in question
 * \param member the name of the struct list_head member
 * \note If you want to destroy or remove members from the list, don't use this iterator
 */
#define list_for_each_type(iterator, head, member)                                  \
    for (iterator = LIST_ITEM((head)->next, __typeof__(*iterator), member);         \
         &iterator->member != (head);                                               \
         iterator = LIST_ITEM((iterator)->member.next, __typeof__(*iterator), member))

/**
 * \brief Iterate through all items in a linked list, in reverse, returning items by type.
 * Designed to be used like a for-loop.
 *
 * \param iterator the iterator
 * \param head the head of the list.
 * \param member The name of the struct list_head member
 */
#define list_for_each_type_reverse(iterator, head, member)                          \
    for ((iterator) = LIST_ITEM((head)->prev, __typeof__(*iterator), member);       \
         &(iterator)->member != (head);                                             \
         iterator = LIST_ITEM((iterator)->member.prev, __typeof__(*iterator), member))
/**
 * \brief Iterate through all items in a linked list, by type
 * Iterate through a linked list and populate the provided iterator,
 * a pointer of the specified container type, with a pointer to
 * the item in the list. Start the iteration at item start.
 * \param iterator an iterator of the type in which the struct list is embedded
 * \param start the first item to start the search from
 * \param head the struct list_head in question
 * \param member the name of the struct list_head member
 * \note If you want to destroy or remove members from the list, don't use this iterator
 */
#define list_for_each_type_start(iterator, start, head, member)                               \
    for (iterator = LIST_ITEM(start, __typeof__(*iterator), member);          \
         &iterator->member != (head);                                            \
         iterator = LIST_ITEM((iterator)->member.next, __typeof__(*iterator), member))

/**
 * \brief Safely iterate through an entire linked list with a cursor.
 * Perform an interation through a linked list without generating a pointer to a countainer
 * type.
 */
#define list_for_each_safe(iter, temp, head) \
    for (iter = (head)->next,                       \
            temp = iter->next;                      \
         iter != (head);                            \
         iter = temp,                               \
            temp = iter->next)

/**
 * \brief Iterate through all items in a linked list, with a cursor.
 * Iterate through all items in a linked list and populate the provided iterator,
 * managing the pointer to the next item in the list using a temporary look-ahead value.
 * This iterator is safe for deletion of items.
 * \param iterator The cursor
 * \param temp A temporary item used to cache the look-ahead
 * \param head The head of the linked list
 * \param member The name of the member
 */
#define list_for_each_type_safe(iterator, temp, head, member) \
    for (iterator = LIST_ITEM((head)->next, __typeof__(*iterator), member), \
            temp = LIST_ITEM(iterator->member.next, __typeof__(*iterator), member); \
         &iterator->member != (head); \
         iterator = temp, temp = LIST_ITEM(temp->member.next, __typeof__(*iterator), member))

/**
 * \brief Determine if a list is empty
 */
static inline
int list_empty(struct list_entry *head)
{
    return !!(head->next == head);
}

static inline
int list_is_last(struct list_entry *head, struct list_entry *item)
{
    return !!(item->next == head);
}


/* Initialize a list item to an empty state */
static inline
void list_init(struct list_entry *_new)
{
    _new->next = _new->prev = _new;
}

/* Private function for splicing an item into a list */
static inline
void __list_add(struct list_entry *_new,
                struct list_entry *prev,
                struct list_entry *next)
{
    prev->next = _new;
    _new->prev = prev;
    next->prev = _new;
    _new->next = next;
}

/**
 * \brief Insert an item in the list after the specified item pos.
 * Given a prior item, insert the given item into the list after the given item.
 */
static inline
void list_insert_at(struct list_entry *pos, struct list_entry *_new)
{
    __list_add(_new, pos, pos->next);
}

static inline
void list_debug_insert_at(struct list_entry* head,
                          struct list_entry *pos,
                          struct list_entry *_new)
{
    struct list_entry* iter;
    list_for_each(iter, head) {
        if (iter == _new) {
            fprintf(stderr, "entry: %p already a member of %p\n", _new, head);
            abort();
        }
    }
    __list_add(_new, pos, pos->next);
}

/**
 * \brief Add a new item to the list
 * Adds a new item to the linked list. This is an O(1) operation.
 */
static inline
void list_append(struct list_entry *head, struct list_entry *_new)
{
    __list_add(_new, head->prev, head);
}

/**
 * \brief A debugging-aid to add a new item to the list
 * This iterates through the list, ensuring that the item isn't already
 * added to the list
 */
static inline
void list_debug_append(struct list_entry *head, struct list_entry *_new)
{
    struct list_entry* iter;
    list_for_each(iter, head) {
        if (iter == _new) {
            printf("entry: %p already a member of %p\n", _new, head);
            abort();
        }
    }
    __list_add(_new, head->prev, head);
}

/**
 * \brief A debugging-aid to add a new item to the beginning of the list
 * This iterates through the list, ensuring that the item isn't already
 * added to the list
 */
static inline
void list_debug_prepend(struct list_entry *head, struct list_entry *_new)
{
    struct list_entry* iter;
    list_for_each(iter, head) {
        if (iter == _new) {
            printf("entry: %p already a member of %p\n", _new, head);
            abort();
        }
    }
    __list_add(_new, head, head->next);
}

/**
 * \brief Add a new item to the list at the front
 * Adds a new item to the linked list at the front. This is an O(1) operation.
 */
static inline
void list_prepend(struct list_entry *head, struct list_entry *_new)
{
    __list_add(_new, head, head->next);
}

/* Private function for removing an item from a list */
static inline
void __list_del(struct list_entry *prev, struct list_entry *next)
{
    prev->next = next;
    next->prev = prev;
}

/**
 * \brief Delete an item from a linked list
 * Delete the specified item from the linked list. This is an O(1) operation.
 */
static inline
void list_del(struct list_entry *del)
{
    __list_del(del->prev, del->next);
    del->next = (void *)LIST_POISON_NEXT;
    del->prev = (void *)LIST_POISON_PREV;
}

/* Private function to splice two linked lists together */
static inline
void __list_splice(struct list_entry *splice,
                   struct list_entry *prev,
                   struct list_entry *next)
{
    /* Splice the two lists together */
    struct list_entry *f = splice->next;
    struct list_entry *l = splice->prev;

    f->prev = prev;
    prev->next = f;

    l->next = next;
    next->prev = l;
}

/**
 * \brief Given two list heads, splice the lists
 * Splice the given two list heads into a single, large list, attached to the
 * specified new head, before the head list's contents
 */
static inline
void list_splice(struct list_entry *list,
                 struct list_entry *head)
{
    if (!list_empty(list)) {
        __list_splice(list, head, head->next);
    }
}

/**
 * \brief Given two list heads, splice the lists
 * Splice the given two list heads into a single, large list, attached to the
 * specified new head after the head list's contents.
 */
static inline
void list_splice_after(struct list_entry *list,
                 struct list_entry *head)
{
    if (!list_empty(list)) {
        __list_splice(list, head->prev, head);
    }
}

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */
