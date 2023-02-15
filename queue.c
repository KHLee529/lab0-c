#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

/* Notice: sometimes, Cppcheck would find the potential NULL pointer bugs,
 * but some of them cannot occur. You can suppress them by adding the
 * following line.
 *   cppcheck-suppress nullPointer
 */


/* Create an empty queue */
struct list_head *q_new()
{
    struct list_head *head =
        (struct list_head *) malloc(sizeof(struct list_head));
    if (head)
        INIT_LIST_HEAD(head);
    return head;
}

/* Free all storage used by queue */
void q_free(struct list_head *l)
{
    struct list_head *node, *safe;
    list_for_each_safe (node, safe, l) {
        element_t *entry = list_entry(node, element_t, list);
        list_del(node);
        q_release_element(entry);
    }
    free(l);
}

static inline element_t *new_elem(char *s)
{
    element_t *entry = (element_t *) malloc(sizeof(element_t));
    if (!entry)
        return NULL;
    size_t cplen = strlen(s);
    if (cplen > cplen + 1) {
        free(entry);
        return NULL;
    }
    entry->value = (char *) malloc(cplen * sizeof(char));
    if (entry->value) {
        memcpy(entry->value, s, cplen);
        INIT_LIST_HEAD(&entry->list);
    } else {
        free(entry);
        entry = NULL;
    }
    return entry;
}

/* Insert an element at head of queue */
bool q_insert_head(struct list_head *head, char *s)
{
    if (!head)
        return false;
    element_t *entry = new_elem(s);
    if (entry)
        list_add(&entry->list, head);
    return !!entry;
}

/* Insert an element at tail of queue */
bool q_insert_tail(struct list_head *head, char *s)
{
    if (!head)
        return false;
    element_t *entry = new_elem(s);
    if (entry)
        list_add_tail(&entry->list, head);
    return !!entry;
}

static inline void remove_elem(element_t *node, char *sp, size_t bufsize)
{
    list_del_init(&node->list);
    if (!sp)
        return;
    size_t cplen = strlen(node->value) + 1;
    sp[bufsize - 1] = '\0';
    memcpy(sp, node->value, (cplen > bufsize ? bufsize - 1 : cplen));
}

/* Remove an element from head of queue */
element_t *q_remove_head(struct list_head *head, char *sp, size_t bufsize)
{
    if (!head || list_empty(head))
        return NULL;
    element_t *entry = list_first_entry(head, element_t, list);
    remove_elem(entry, sp, bufsize);
    return entry;
}

/* Remove an element from tail of queue */
element_t *q_remove_tail(struct list_head *head, char *sp, size_t bufsize)
{
    if (!head || list_empty(head))
        return NULL;
    element_t *entry = list_last_entry(head, element_t, list);
    remove_elem(entry, sp, bufsize);
    return entry;
}

/* Return number of elements in queue */
int q_size(struct list_head *head)
{
    if (!head || list_empty(head))
        return 0;
    int size = 0;
    struct list_head *node;
    list_for_each (node, head)
        size += 1;
    return size;
}

/* Delete the middle node in queue */
bool q_delete_mid(struct list_head *head)
{
    // https://leetcode.com/problems/delete-the-middle-node-of-a-linked-list/
    if (!head || list_empty(head))
        return false;
    struct list_head *fw = head->next, *bw = head->prev;
    while (fw != bw && fw->next != bw) {
        fw = fw->next;
        bw = bw->prev;
    }
    list_del(fw);
    element_t *tmp = list_entry(fw, element_t, list);
    q_release_element(tmp);
    return true;
}

/* Delete all nodes that have duplicate string */
bool q_delete_dup(struct list_head *head)
{
    // https://leetcode.com/problems/remove-duplicates-from-sorted-list-ii/
    if (!head)
        return false;
    element_t *entry, *next;
    char *del_val = NULL;
    list_for_each_entry_safe (entry, next, head, list) {
        if (del_val && !strcmp(entry->value, del_val)) {
            list_del(&entry->list);
            q_release_element(entry);
            continue;
        }
        if (del_val) {
            free(del_val);
            del_val = NULL;
        }
        if (&next->list != head && !strcmp(entry->value, next->value)) {
            del_val = entry->value;
            entry->value = NULL;
            list_del(&entry->list);
            q_release_element(entry);
        }
    }
    free(del_val);
    return true;
}

/* Swap every two adjacent nodes */
void q_swap(struct list_head *head)
{
    // https://leetcode.com/problems/swap-nodes-in-pairs/
    if (!head || list_empty(head))
        return;
    struct list_head *node, *safe, *swap = NULL;
    list_for_each_safe (node, safe, head) {
        if (swap) {
            list_add(swap, node);
            swap = NULL;
        } else if (safe != head) {
            swap = node;
            list_del_init(node);
        }
    }
}

/* Reverse elements in queue */
void q_reverse(struct list_head *head)
{
    if (!head || list_empty(head))
        return;
    struct list_head *node, *safe;
    list_for_each_safe (node, safe, head) {
        list_del_init(node);
        list_add(node, head);
    }
}

/* Reverse the nodes of the list k at a time */
void q_reverseK(struct list_head *head, int k)
{
    // https://leetcode.com/problems/reverse-nodes-in-k-group/
    if (!head || list_empty(head) || k < 2)
        return;
    LIST_HEAD(tmp);
    struct list_head *cur, *safe, *sub_st = head;
    int cnt = 0;
    list_for_each_safe (cur, safe, head) {
        cnt += 1;
        if (cnt == k) {
            list_cut_position(&tmp, sub_st, cur);
            q_reverse(&tmp);
            list_splice_init(&tmp, sub_st);
            cnt = 0;
            sub_st = safe->prev;
        }
    }
}
}

/* Sort elements of queue in ascending order */
void q_sort(struct list_head *head) {}

/* Remove every node which has a node with a strictly greater value anywhere to
 * the right side of it */
int q_descend(struct list_head *head)
{
    // https://leetcode.com/problems/remove-nodes-from-linked-list/
    return 0;
}

/* Merge all the queues into one sorted queue, which is in ascending order */
int q_merge(struct list_head *head)
{
    // https://leetcode.com/problems/merge-k-sorted-lists/
    return 0;
}
