#pragma once

#include <stdbool.h>

struct queue_private;
struct queue_shared;

struct qitem {
    struct qitem *next;
};

struct queue_private_iter {
    struct qitem *pos;
};

typedef void (*queue_dtor_t)(struct qitem *item);

void qitem_init(struct qitem *qi);
void qitem_fini(struct qitem *qi);
bool qitem_is_queued(const struct qitem *qi);

int queue_private_alloc(struct queue_private **out);
void queue_private_free(struct queue_private *qp, queue_dtor_t dtor);
void queue_private_move_from_shared(
        struct queue_private *qp,
        struct queue_shared *qs);
bool queue_private_is_empty(const struct queue_private *qp);
struct qitem *queue_private_pop(struct queue_private *qp);

void queue_private_iter_init(
        struct queue_private_iter *i,
        struct queue_private *qp);
bool queue_private_iter_is_valid(const struct queue_private_iter *i);
void queue_private_iter_next(struct queue_private_iter *i);
struct qitem *queue_private_iter_deref(const struct queue_private_iter *i);

int queue_shared_alloc(struct queue_shared **out);
void queue_shared_free(struct queue_shared *qs, queue_dtor_t dtor);
void queue_shared_move_from_private(
        struct queue_shared *qs,
        struct queue_private *qp);
void queue_shared_push(struct queue_shared *qs, struct qitem *qi);
