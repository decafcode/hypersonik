#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include "list.h"

struct list {
    struct list_node *head;
    struct list_node *tail;
};

void list_node_init(struct list_node *node)
{
    assert(node != NULL);

    node->prev = node;
    node->next = node;
}

void list_node_fini(struct list_node *node)
{
    assert(node != NULL);
    assert(!list_node_is_inserted(node));

    node->prev = (void *) -1L;
    node->next = (void *) -1L;
}

bool list_node_is_inserted(struct list_node *node)
{
    assert(node != NULL);

    return node->prev != node || node->next != node;
}

int list_alloc(struct list **out)
{
    assert(out != NULL);

    *out = calloc(sizeof(**out), 1);

    if (*out == NULL) {
        return -ENOMEM;
    }

    return 0;
}

void list_free(struct list *list, list_dtor_t dtor)
{
    struct list_iter i;
    struct list_node *node;

    if (list == NULL) {
        return;
    }

    list_iter_init(&i, list);

    while (list_iter_is_valid(&i)) {
        node = list_iter_deref(&i);
        list_iter_next(&i);

        if (dtor != NULL) {
            dtor(node);
        }

        list_remove(list, node);
    }

    free(list);
}

void list_append(struct list *list, struct list_node *node)
{
    struct list_node *tail;

    assert(list != NULL);
    assert(node != NULL);
    assert(!list_node_is_inserted(node));

    tail = list->tail;
    node->prev = tail;
    node->next = NULL;

    if (tail != NULL) {
        assert(tail->next == NULL);
        assert(tail->prev == NULL || tail->prev->next == tail);
        assert(tail->prev != NULL || list->tail == tail);

        tail->next = node;
    } else {
        assert(list->head == NULL);

        list->head = node;
    }

    list->tail = node;
}

void list_remove(struct list *list, struct list_node *node)
{
    struct list_node *prev;
    struct list_node *next;

    assert(list != NULL);
    assert(node != NULL);
    assert(list_node_is_inserted(node));

    if (node->prev != NULL) {
        prev = node->prev;
        assert(prev->next == node);
        prev->next = node->next;
    } else {
        assert(list->head == node);
        list->head = node->next;
    }

    if (node->next != NULL) {
        next = node->next;
        assert(next->prev == node);
        next->prev = node->prev;
    } else {
        assert(list->tail == node);
        list->tail = node->prev;
    }

    node->prev = node;
    node->next = node;
}

void list_iter_init(struct list_iter *i, struct list *list)
{
    assert(i != NULL);
    assert(list != NULL);

    i->pos = list->head;
#ifndef NDEBUG
    i->list = list;
#endif
}

bool list_iter_is_valid(const struct list_iter *i)
{
    assert(i != NULL);

    return i->pos != NULL;
}

void list_iter_next(struct list_iter *i)
{
    const struct list_node *pos;
#ifndef NDEBUG
    const struct list_node *prev;
    const struct list_node *next;
    const struct list *list;
#endif

    assert(i != NULL);
    assert(i->pos != NULL);

    pos = i->pos;

#ifndef NDEBUG
    prev = i->pos->prev;
    next = i->pos->next;
    list = i->list;
#endif

    assert(prev == NULL || prev->next == pos);
    assert(prev != NULL || list->head == pos);
    assert(next == NULL || next->prev == pos);
    assert(next != NULL || list->tail == pos);

    i->pos = pos->next;
}

struct list_node *list_iter_deref(const struct list_iter *i)
{
    assert(i != NULL);
    assert(i->pos != NULL);

    return i->pos;
}
