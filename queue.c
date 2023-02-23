#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"

/* Notice: sometimes, Cppcheck would find the potential NULL pointer bugs,
 * but some of them cannot occur. You can suppress them by adding the
 * following line.
 *   cppcheck-suppress nullPointer
 */

/*
 * define `USE_LINUX_SORT` at compile time if you want to use the `list_sort`
 * function implemented in linux as q_sort
 */
#ifdef USE_LINUX_SORT

typedef int
    __attribute__((nonnull(2, 3))) (*list_cmp_func_t)(void *,
                                                      const struct list_head *,
                                                      const struct list_head *);

int sort_cmp(void *priv, const struct list_head *a, const struct list_head *b)
{
    const char *a_val = list_entry(a, element_t, list)->value;
    const char *b_val = list_entry(b, element_t, list)->value;
    return strcmp(a_val, b_val);
}

/*
 * Returns a list organized in an intermediate format suited
 * to chaining of merge() calls: null-terminated, no reserved or
 * sentinel head node, "prev" links not maintained.
 */
__attribute__((nonnull(2, 3, 4))) static struct list_head *
merge(void *priv, list_cmp_func_t cmp, struct list_head *a, struct list_head *b)
{
    struct list_head *head = NULL, **tail = &head;

    for (;;) {
        /* if equal, take 'a' -- important for sort stability */
        if (cmp(priv, a, b) <= 0) {
            *tail = a;
            tail = &a->next;
            a = a->next;
            if (!a) {
                *tail = b;
                break;
            }
        } else {
            *tail = b;
            tail = &b->next;
            b = b->next;
            if (!b) {
                *tail = a;
                break;
            }
        }
    }
    return head;
}

/*
 * Combine final list merge with restoration of standard doubly-linked
 * list structure.  This approach duplicates code from merge(), but
 * runs faster than the tidier alternatives of either a separate final
 * prev-link restoration pass, or maintaining the prev links
 * throughout.
 */
__attribute__((nonnull(2, 3, 4, 5))) static void merge_final(
    void *priv,
    list_cmp_func_t cmp,
    struct list_head *head,
    struct list_head *a,
    struct list_head *b)
{
    struct list_head *tail = head;
    unsigned int count = 0;

    for (;;) {
        /* if equal, take 'a' -- important for sort stability */
        if (cmp(priv, a, b) <= 0) {
            tail->next = a;
            a->prev = tail;
            tail = a;
            a = a->next;
            if (!a)
                break;
        } else {
            tail->next = b;
            b->prev = tail;
            tail = b;
            b = b->next;
            if (!b) {
                b = a;
                break;
            }
        }
    }

    /* Finish linking remainder of list b on to tail */
    tail->next = b;
    do {
        /*
         * If the merge is highly unbalanced (e.g. the input is
         * already sorted), this loop may run many iterations.
         * Continue callbacks to the client even though no
         * element comparison is needed, so the client's cmp()
         * routine can invoke cond_resched() periodically.
         */
        if (!++count)
            cmp(priv, b, b);
        b->prev = tail;
        tail = b;
        b = b->next;
    } while (b);

    /* And the final links to make a circular doubly-linked list */
    tail->next = head;
    head->prev = tail;
}

/**
 * list_sort - sort a list
 * @priv: private data, opaque to list_sort(), passed to @cmp
 * @head: the list to sort
 * @cmp: the elements comparison function
 *
 * The comparison function @cmp must return > 0 if @a should sort after
 * @b ("@a > @b" if you want an ascending sort), and <= 0 if @a should
 * sort before @b *or* their original order should be preserved.  It is
 * always called with the element that came first in the input in @a,
 * and list_sort is a stable sort, so it is not necessary to distinguish
 * the @a < @b and @a == @b cases.
 *
 * This is compatible with two styles of @cmp function:
 * - The traditional style which returns <0 / =0 / >0, or
 * - Returning a boolean 0/1.
 * The latter offers a chance to save a few cycles in the comparison
 * (which is used by e.g. plug_ctx_cmp() in block/blk-mq.c).
 *
 * A good way to write a multi-word comparison is::
 *
 *	if (a->high != b->high)
 *		return a->high > b->high;
 *	if (a->middle != b->middle)
 *		return a->middle > b->middle;
 *	return a->low > b->low;
 *
 *
 * This mergesort is as eager as possible while always performing at least
 * 2:1 balanced merges.  Given two pending sublists of size 2^k, they are
 * merged to a size-2^(k+1) list as soon as we have 2^k following elements.
 *
 * Thus, it will avoid cache thrashing as long as 3*2^k elements can
 * fit into the cache.  Not quite as good as a fully-eager bottom-up
 * mergesort, but it does use 0.2*n fewer comparisons, so is faster in
 * the common case that everything fits into L1.
 *
 *
 * The merging is controlled by "count", the number of elements in the
 * pending lists.  This is beautifully simple code, but rather subtle.
 *
 * Each time we increment "count", we set one bit (bit k) and clear
 * bits k-1 .. 0.  Each time this happens (except the very first time
 * for each bit, when count increments to 2^k), we merge two lists of
 * size 2^k into one list of size 2^(k+1).
 *
 * This merge happens exactly when the count reaches an odd multiple of
 * 2^k, which is when we have 2^k elements pending in smaller lists,
 * so it's safe to merge away two lists of size 2^k.
 *
 * After this happens twice, we have created two lists of size 2^(k+1),
 * which will be merged into a list of size 2^(k+2) before we create
 * a third list of size 2^(k+1), so there are never more than two pending.
 *
 * The number of pending lists of size 2^k is determined by the
 * state of bit k of "count" plus two extra pieces of information:
 *
 * - The state of bit k-1 (when k == 0, consider bit -1 always set), and
 * - Whether the higher-order bits are zero or non-zero (i.e.
 *   is count >= 2^(k+1)).
 *
 * There are six states we distinguish.  "x" represents some arbitrary
 * bits, and "y" represents some arbitrary non-zero bits:
 * 0:  00x: 0 pending of size 2^k;           x pending of sizes < 2^k
 * 1:  01x: 0 pending of size 2^k; 2^(k-1) + x pending of sizes < 2^k
 * 2: x10x: 0 pending of size 2^k; 2^k     + x pending of sizes < 2^k
 * 3: x11x: 1 pending of size 2^k; 2^(k-1) + x pending of sizes < 2^k
 * 4: y00x: 1 pending of size 2^k; 2^k     + x pending of sizes < 2^k
 * 5: y01x: 2 pending of size 2^k; 2^(k-1) + x pending of sizes < 2^k
 * (merge and loop back to state 2)
 *
 * We gain lists of size 2^k in the 2->3 and 4->5 transitions (because
 * bit k-1 is set while the more significant bits are non-zero) and
 * merge them away in the 5->2 transition.  Note in particular that just
 * before the 5->2 transition, all lower-order bits are 11 (state 3),
 * so there is one list of each smaller size.
 *
 * When we reach the end of the input, we merge all the pending
 * lists, from smallest to largest.  If you work through cases 2 to
 * 5 above, you can see that the number of elements we merge with a list
 * of size 2^k varies from 2^(k-1) (cases 3 and 5 when x == 0) to
 * 2^(k+1) - 1 (second merge of case 5 when x == 2^(k-1) - 1).
 */
__attribute__((nonnull(2, 3))) void list_sort(void *priv,
                                              struct list_head *head,
                                              list_cmp_func_t cmp)
{
    struct list_head *list = head->next, *pending = NULL;
    size_t count = 0; /* Count of pending */

    if (list == head->prev) /* Zero or one elements */
        return;

    /* Convert to a null-terminated singly-linked list. */
    head->prev->next = NULL;

    /*
     * Data structure invariants:
     * - All lists are singly linked and null-terminated; prev
     *   pointers are not maintained.
     * - pending is a prev-linked "list of lists" of sorted
     *   sublists awaiting further merging.
     * - Each of the sorted sublists is power-of-two in size.
     * - Sublists are sorted by size and age, smallest & newest at front.
     * - There are zero to two sublists of each size.
     * - A pair of pending sublists are merged as soon as the number
     *   of following pending elements equals their size (i.e.
     *   each time count reaches an odd multiple of that size).
     *   That ensures each later final merge will be at worst 2:1.
     * - Each round consists of:
     *   - Merging the two sublists selected by the highest bit
     *     which flips when count is incremented, and
     *   - Adding an element from the input as a size-1 sublist.
     */
    do {
        size_t bits;
        struct list_head **tail = &pending;

        /* Find the least-significant clear bit in count */
        for (bits = count; bits & 1; bits >>= 1)
            tail = &(*tail)->prev;
        /* Do the indicated merge */
        if (bits) {
            struct list_head *a = *tail, *b = a->prev;

            a = merge(priv, cmp, b, a);
            /* Install the merged result in place of the inputs */
            a->prev = b->prev;
            *tail = a;
        }

        /* Move one element from input list to pending */
        list->prev = pending;
        pending = list;
        list = list->next;
        pending->next = NULL;
        count++;
    } while (list);

    /* End of input; merge together all the pending lists. */
    list = pending;
    pending = pending->prev;
    for (;;) {
        struct list_head *next = pending->prev;

        if (!next)
            break;
        list = merge(priv, cmp, pending, list);
        pending = next;
    }
    /* The final merge, rebuilding prev links */
    merge_final(priv, cmp, head, pending, list);
}
#endif /* USE_LINUX_SORT */

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

    size_t cplen = strlen(s) + 1;
    entry->value = malloc(cplen * sizeof(char));
    if (!entry->value) {
        free(entry);
        return NULL;
    }

    memcpy(entry->value, s, cplen);
    INIT_LIST_HEAD(&entry->list);
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
        list_move(node, head);
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

/* get the cut point in @head until which the value is smaller than @s */
static struct list_head *get_cut(struct list_head *head, char *s)
{
    element_t *entry = NULL, *safe = NULL;
    list_for_each_entry_safe (entry, safe, head, list) {
        if (strcmp(entry->value, s) > 0)
            break;
    }
    entry = list_entry(entry->list.prev, element_t, list);
    return &entry->list;
}

/* merge two list and return the total node amount */
static void merge_two_list(struct list_head *l1,
                           struct list_head *l2,
                           struct list_head *dest)
{
    if (!l2 || list_empty(l2)) {
        list_splice_init(l1, dest);
        return;
    }
    LIST_HEAD(tmp);
    struct list_head *lists[2] = {l1, l2};
    struct list_head *cut = NULL;
    int flag = 0;
    while (!list_empty(l1) && !list_empty(l2)) {
        cut = get_cut(lists[flag],
                      list_first_entry(lists[!flag], element_t, list)->value);
        list_cut_position(&tmp, lists[flag], cut);
        list_splice_tail_init(&tmp, dest);
        flag = !flag;
    }
    for (int i = 0; i < 2; i++) {
        list_splice_tail_init(lists[i], dest);
    }
}
/* Sort elements of queue in ascending order */
void q_sort(struct list_head *head)
{
#ifndef USE_LINUX_SORT
    if (list_empty(head) || list_is_singular(head))
        return;

    LIST_HEAD(h1);
    LIST_HEAD(h2);
    struct list_head *fw = head->next, *bw = head->prev;
    while (fw != bw && fw->next != bw) {
        fw = fw->next;
        bw = bw->prev;
    }
    list_cut_position(&h2, head, fw);
    list_splice_init(head, &h1);
    q_sort(&h1);
    q_sort(&h2);
    merge_two_list(&h1, &h2, head);
#else
    list_sort(NULL, head, sort_cmp);
#endif
}

/* Remove every node which has a node with a strictly greater value anywhere to
 * the right side of it */
int q_descend(struct list_head *head)
{
    // https://leetcode.com/problems/remove-nodes-from-linked-list/
    char *max = NULL;
    element_t *entry = NULL, *safe = NULL;
    int total = 0, n_del = 0;
    for (entry = list_entry(head->prev, element_t, list),
        safe = list_entry(entry->list.prev, element_t, list);
         &entry->list != head;
         entry = safe, safe = list_entry(safe->list.prev, element_t, list)) {
        total += 1;
        if (!max || strcmp(entry->value, max) > 0) {
            max = entry->value;
        } else {
            list_del(&entry->list);
            q_release_element(entry);
            n_del += 1;
        }
    }
    return total - n_del;
}



/* Merge all the queues into one sorted queue, which is in ascending order */
int q_merge(struct list_head *head)
{
    // https://leetcode.com/problems/merge-k-sorted-lists/
    if (list_is_singular(head))
        return list_entry(head, queue_contex_t, chain)->size;

    LIST_HEAD(q_tmp);
    struct list_head *dest = head->next, *end = head;
    struct list_head *merge_p = head->next;
    queue_contex_t *q1 = NULL, *q2 = NULL, *q_dest = NULL;
    int size_tmp;
    while (end != merge_p->next) {
        while (merge_p != end && merge_p->next != end) {
            q1 = list_entry(merge_p, queue_contex_t, chain);
            q2 = list_entry(merge_p->next, queue_contex_t, chain);
            q_dest = list_entry(dest, queue_contex_t, chain);

            merge_two_list(q1->q, q2->q, &q_tmp);
            size_tmp = q1->size + q2->size;
            q1->size = q2->size = 0;

            list_splice_init(&q_tmp, q_dest->q);
            q_dest->size = size_tmp;

            merge_p = merge_p->next->next;
            dest = dest->next;
        }
        if (merge_p != end) {
            q1 = list_entry(merge_p, queue_contex_t, chain);
            q_dest = list_entry(dest, queue_contex_t, chain);

            size_tmp = q1->size;
            q1->size = 0;

            list_splice_init(q1->q, q_dest->q);
            q_dest->size = size_tmp;

            dest = dest->next;
            merge_p = merge_p->next;
        }
        end = dest;
        merge_p = head->next;
        dest = head->next;
    }
    q1 = list_entry(merge_p, queue_contex_t, chain);
    return q1->size;
}
