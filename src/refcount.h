#ifndef HYPERSONIK_REFCOUNT_H
#define HYPERSONIK_REFCOUNT_H

#include <stdatomic.h>

/* Callback for releasing parent reference counts */
typedef void (*dtor_notify_t)(void *ctx);

/* Atomic reference count */
typedef atomic_uint refcount_t;

/* Explicit memory_order on x86 is basically meaningless anyway but w/e */

inline unsigned int refcount_inc(refcount_t *rc)
{
    unsigned int old_rc;

    old_rc = atomic_fetch_add_explicit(rc, 1, memory_order_relaxed);

    return old_rc + 1;
}

inline unsigned int refcount_dec(refcount_t *rc)
{
    unsigned int old_rc;

    old_rc = atomic_fetch_sub_explicit(rc, 1, memory_order_acq_rel);

    return old_rc - 1;
}

#endif
