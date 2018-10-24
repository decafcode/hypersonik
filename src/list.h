#pragma once

#include <stdbool.h>

struct list;

struct list_node {
    struct list_node *prev;
    struct list_node *next;
};

struct list_iter {
    struct list_node *pos;
#ifndef NDEBUG
    const struct list *list;
#endif
};

typedef void (*list_dtor_t)(struct list_node *node);

void list_node_init(struct list_node *node);
void list_node_fini(struct list_node *node);
bool list_node_is_inserted(struct list_node *node);

int list_alloc(struct list **out);
void list_free(struct list *list, list_dtor_t dtor);
void list_append(struct list *list, struct list_node *node);
void list_remove(struct list *list, struct list_node *node);

void list_iter_init(struct list_iter *i, struct list *list);
bool list_iter_is_valid(const struct list_iter *i);
void list_iter_next(struct list_iter *i);
struct list_node *list_iter_deref(const struct list_iter *i);
