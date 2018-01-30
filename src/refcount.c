#include "refcount.h"

extern inline unsigned int refcount_inc(refcount_t *rc);
extern inline unsigned int refcount_dec(refcount_t *rc);
