#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "queue.h"

struct queue_private {
    /* Elements are stored in the correct order */
    struct qitem *head;
};

struct queue_shared {
    /* Elements are stacked in reverse order */
    struct qitem *tail;
};

static struct qitem *qitem_chain_reverse(struct qitem *qi);
static void queue_common_fini(struct qitem *qi, queue_dtor_t dtor);

void qitem_init(struct qitem *qi)
{
    assert(qi != NULL);

    qi->next = qi;
}

void qitem_fini(struct qitem *qi)
{
    assert(qi != NULL);
    assert(qi->next == qi);

    qi->next = (void *) -1L;
}

bool qitem_is_queued(const struct qitem *qi)
{
    assert(qi != NULL);

    return qi->next != qi;
}

static struct qitem *qitem_chain_reverse(struct qitem *qi)
{
    struct qitem *next;
    struct qitem *prev;

    if (qi == NULL) {
        return NULL;
    }

    prev = NULL;

    for (;;) {
        next = qi->next;
        qi->next = prev;

        if (next == NULL) {
            return qi;
        }

        prev = qi;
        qi = next;
    }
}

static void queue_common_fini(struct qitem *qi, queue_dtor_t dtor)
{
    struct qitem *next;

    while (qi != NULL) {
        next = qi->next;
        qi->next = qi;

        if (dtor != NULL) {
            dtor(qi);
        }

        qi = next;
    }
}

int queue_private_alloc(struct queue_private **out)
{
    assert(out != NULL);

    *out = calloc(sizeof(**out), 1);

    if (*out == NULL) {
        return -ENOMEM;
    }

    return 0;
}

void queue_private_free(struct queue_private *qp, queue_dtor_t dtor)
{
    if (qp == NULL) {
        return;
    }

    queue_common_fini(qp->head, dtor);
    free(qp);
}

void queue_private_move_from_shared(
        struct queue_private *qp,
        struct queue_shared *qs)
{
    struct qitem *tail;

    assert(qp != NULL);
    // Something is wrong here, need to investigate
    //assert(qp->head == NULL); /* Sufficient for our purposes */
    assert(qs != NULL);

    do {
        tail = qs->tail;
    } while (!atomic_compare_exchange_weak(&qs->tail, &tail, NULL));

    qp->head = qitem_chain_reverse(tail);
}

bool queue_private_is_empty(const struct queue_private *qp)
{
    assert(qp != NULL);

    return qp->head == NULL;
}

struct qitem *queue_private_pop(struct queue_private *qp)
{
    struct qitem *qi;

    assert(qp != NULL);

    qi = qp->head;

    if (qi != NULL) {
        qp->head = qi->next;
        qi->next = qi;
    }

    return qi;
}

void queue_private_iter_init(
        struct queue_private_iter *i,
        struct queue_private *qp)
{
    assert(i != NULL);

    i->pos = qp->head;
}

bool queue_private_iter_is_valid(const struct queue_private_iter *i)
{
    assert(i != NULL);

    return i->pos != NULL;
}

void queue_private_iter_next(struct queue_private_iter *i)
{
    assert(i != NULL);
    assert(i->pos != NULL);

    i->pos = i->pos->next;
}

struct qitem *queue_private_iter_deref(const struct queue_private_iter *i)
{
    assert(i != NULL);
    assert(i->pos != NULL);

    return i->pos;
}

int queue_shared_alloc(struct queue_shared **out)
{
    assert(out != NULL);

    *out = calloc(sizeof(**out), 1);

    if (*out == NULL) {
        return -ENOMEM;
    }

    return 0;
}

void queue_shared_free(struct queue_shared *qs, queue_dtor_t dtor)
{
    if (qs == NULL) {
        return;
    }

    queue_common_fini(qs->tail, dtor);
    free(qs);
}

void queue_shared_move_from_private(
        struct queue_shared *qs,
        struct queue_private *qp)
{
    struct qitem *tail;
    struct qitem *tmp;

    assert(qs != NULL);
    assert(qp != NULL);

    if (qp->head == NULL) {
        return;
    }

    tail = qp->head;
    qitem_chain_reverse(tail);

    assert(tail->next == NULL);

    do {
        tmp = qs->tail;
        tail->next = tmp;
    } while (!atomic_compare_exchange_weak(&qs->tail, &tmp, tail));
}

void queue_shared_push(struct queue_shared *qs, struct qitem *qi)
{
    struct qitem *tail;

    assert(qs != NULL);
    assert(qi != NULL);

    do {
        tail = qs->tail;
        qi->next = tail;
    } while (!atomic_compare_exchange_weak(&qs->tail, &tail, qi));
}
