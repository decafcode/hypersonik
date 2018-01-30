#include <windows.h>
#include <dsound.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"
#include "ds-buffer-pri.h"
#include "refcount.h"
#include "trace.h"

struct ds_buffer_pri {
    IDirectSoundBuffer com;
    refcount_t rc;
};

static IDirectSoundBufferVtbl ds_buffer_pri_vtbl;

HRESULT ds_buffer_pri_alloc(struct ds_buffer_pri **out)
{
    struct ds_buffer_pri *self;

    assert(out != NULL);

    *out = NULL;
    self = calloc(sizeof(*self), 1);

    if (self == NULL) {
        return E_OUTOFMEMORY;
    }

    self->com.lpVtbl = &ds_buffer_pri_vtbl;
    self->rc = 1;

    *out = self;

    return S_OK;
}

struct ds_buffer_pri *ds_buffer_pri_downcast(IDirectSoundBuffer *com)
{
    if (com == NULL) {
        return NULL;
    }

    return containerof(com, struct ds_buffer_pri, com);
}

IDirectSoundBuffer *ds_buffer_pri_upcast(struct ds_buffer_pri *self)
{
    if (self == NULL) {
        return NULL;
    }

    return &self->com;
}

struct ds_buffer_pri *ds_buffer_pri_ref(struct ds_buffer_pri *self)
{
    assert(self != NULL);
    refcount_inc(&self->rc);

    return self;
}

struct ds_buffer_pri *ds_buffer_pri_unref(struct ds_buffer_pri *self)
{
    if (self == NULL || refcount_dec(&self->rc) > 0) {
        return NULL;
    }

    free(self);

    return NULL;
}

static __stdcall HRESULT ds_buffer_pri_query_interface(
        IDirectSoundBuffer *com,
        const IID *iid,
        void **out)
{
    struct ds_buffer_pri *self;

    if (iid == NULL || out == NULL) {
        return E_POINTER;
    }

    *out = NULL;
    self = ds_buffer_pri_downcast(com);

    if (    memcmp(iid, &IID_IDirectSoundBuffer8, sizeof(*iid)) == 0 ||
            memcmp(iid, &IID_IDirectSoundBuffer, sizeof(*iid)) == 0 ||
            memcmp(iid, &IID_IUnknown, sizeof(*iid)) == 0) {
        ds_buffer_pri_ref(self);
        *out = com;

        return S_OK;
    } else {
        return E_NOINTERFACE;
    }
}

static __stdcall ULONG ds_buffer_pri_add_ref(IDirectSoundBuffer *com)
{
    ds_buffer_pri_ref(ds_buffer_pri_downcast(com));

    return 0;
}

static __stdcall ULONG ds_buffer_pri_release(IDirectSoundBuffer *com)
{
    ds_buffer_pri_unref(ds_buffer_pri_downcast(com));

    return 0;
}

static __stdcall HRESULT ds_buffer_pri_set_format(
        IDirectSoundBuffer *com,
        const WAVEFORMATEX *format)
{
    trace("%s(%p) [stub]", __func__, format);

    if (format == NULL) {
        return E_POINTER;
    }

    return S_OK;
}

static struct IDirectSoundBufferVtbl ds_buffer_pri_vtbl = {
    .QueryInterface     = ds_buffer_pri_query_interface,
    .AddRef             = ds_buffer_pri_add_ref,
    .Release            = ds_buffer_pri_release,
    .SetFormat          = ds_buffer_pri_set_format,
};
