#include <windows.h>
#include <dsound.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "defs.h"
#include "ds-buffer.h"
#include "ds-buffer-pri.h"
#include "refcount.h"
#include "trace.h"
#include "wasapi.h"

struct ds_api {
    IDirectSound8 com;
    refcount_t rc;
    CRITICAL_SECTION lock; /* TODO implement locking */
    struct wasapi *wasapi;
};

static HRESULT ds_api_alloc(struct ds_api **out);
static struct ds_api *ds_api_downcast(IDirectSound8 *com);
static IDirectSound8 *ds_api_upcast(struct ds_api *self);
static struct ds_api *ds_api_ref(struct ds_api *self);
static struct ds_api *ds_api_unref(struct ds_api *self);
static void ds_api_unref_notify(void *ptr);
static HRESULT ds_api_start(struct ds_api *self);
static HRESULT ds_api_create_sound_buffer_pri(IDirectSoundBuffer **out);
static HRESULT ds_api_create_sound_buffer_sec(
        struct ds_api *self,
        const DSBUFFERDESC *desc,
        IDirectSoundBuffer **out);

static struct IDirectSound8Vtbl ds_api_vtbl;

static HRESULT ds_api_alloc(struct ds_api **out)
{
    struct ds_api *self;
    HRESULT hr;

    trace_enter();
    assert(out != NULL);

    *out = NULL;
    self = calloc(sizeof(*self), 1);

    if (self == NULL) {
        hr = E_OUTOFMEMORY;

        goto end;
    }

    self->com.lpVtbl = &ds_api_vtbl;
    self->rc = 1;

    hr = wasapi_alloc(&self->wasapi);

    if (FAILED(hr)) {
        goto end;
    }

    *out = ds_api_ref(self);

end:
    ds_api_unref(self);
    trace_exit();

    return hr;
}

static struct ds_api *ds_api_downcast(IDirectSound8 *com)
{
    if (com == NULL) {
        return NULL;
    }

    return containerof(com, struct ds_api, com);
}

static IDirectSound8 *ds_api_upcast(struct ds_api *self)
{
    if (self == NULL) {
        return NULL;
    }

    return &self->com;
}

static struct ds_api *ds_api_ref(struct ds_api *self)
{
    assert(self != NULL);
    refcount_inc(&self->rc);

    return self;
}

static struct ds_api *ds_api_unref(struct ds_api *self)
{
    if (self == NULL || refcount_dec(&self->rc) > 0) {
        return NULL;
    }

    trace("Hypersonik is shutting down");

    wasapi_free(self->wasapi);
    free(self);

    trace("Hypersonik shutdown complete");

    return NULL;
}

static void ds_api_unref_notify(void *ptr)
{
    ds_api_unref(ptr);
}

static HRESULT ds_api_start(struct ds_api *self)
{
    assert(self != NULL);

    return wasapi_start(self->wasapi);
}

static __stdcall HRESULT ds_api_query_interface(
        IDirectSound8 *com,
        const IID *iid,
        void **out)
{
    struct ds_api *self;

    if (iid == NULL || out == NULL) {
        return E_POINTER;
    }

    self = ds_api_downcast(com);

    if (    memcmp(iid, &IID_IDirectSound8, sizeof(*iid)) != 0 &&
            memcmp(iid, &IID_IDirectSound, sizeof(*iid)) != 0 &&
            memcmp(iid, &IID_IUnknown, sizeof(*iid)) != 0) {
        return E_NOINTERFACE;
    }

    *out = ds_api_upcast(ds_api_ref(self));

    return S_OK;
}

static __stdcall ULONG ds_api_add_ref(IDirectSound8 *com)
{
    ds_api_ref(ds_api_downcast(com));

    return 0;
}

static __stdcall ULONG ds_api_release(IDirectSound8 *com)
{
    ds_api_unref(ds_api_downcast(com));

    return 0;
}

static __stdcall HRESULT ds_api_compact(IDirectSound8 *com)
{
    /* ... pls */
    trace("stub %s", __func__);

    return E_NOTIMPL;
}

static __stdcall HRESULT ds_api_create_sound_buffer(
        IDirectSound8 *com,
        const DSBUFFERDESC *desc,
        IDirectSoundBuffer **out,
        IUnknown *outer)
{
    struct ds_api *self;

    self = ds_api_downcast(com);

    if (desc == NULL || out == NULL) {
        return E_POINTER;
    }

    *out = NULL;

    if (outer != NULL) {
        return E_NOTIMPL;
    }

    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
        return ds_api_create_sound_buffer_pri(out);
    } else {
        return ds_api_create_sound_buffer_sec(self, desc, out);
    }
}

static HRESULT ds_api_create_sound_buffer_pri(IDirectSoundBuffer **out)
{
    struct ds_buffer_pri *child;
    HRESULT hr;

    hr = ds_buffer_pri_alloc(&child);

    if (FAILED(hr)) {
        return hr;
    }

    *out = ds_buffer_pri_upcast(ds_buffer_pri_ref(child));

    return S_OK;
}

static HRESULT ds_api_create_sound_buffer_sec(
        struct ds_api *self,
        const DSBUFFERDESC *desc,
        IDirectSoundBuffer **out)
{
    struct snd_client *cli;
    struct ds_buffer *child;
    HRESULT hr;

    child = NULL;
    cli = NULL;

    hr = wasapi_snd_client_alloc(self->wasapi, &cli);

    if (FAILED(hr)) {
        goto end;
    }

    hr = ds_buffer_alloc(
            &child,
            ds_api_unref_notify,
            ds_api_ref(self),
            cli,
            NULL,
            desc->lpwfxFormat,
            wasapi_get_sys_format(self->wasapi),
            desc->dwBufferBytes);

    if (FAILED(hr)) {
        goto end;
    }

    cli = NULL; /* ds_buffer has taken ownership */
    *out = ds_buffer_upcast(ds_buffer_ref(child));

    //trace("%p: Allocated %i byte buffer", child, desc->dwBufferBytes);

end:
    ds_buffer_unref(child);
    snd_client_free(cli);

    return hr;
}

static __stdcall HRESULT ds_api_duplicate_sound_buffer(
        IDirectSound8 *com,
        IDirectSoundBuffer *com_src,
        IDirectSoundBuffer **out)
{
    struct ds_api *self;
    struct ds_buffer *src;
    struct ds_buffer *dest;
    struct snd_client *cli;
    HRESULT hr;

    if (com_src == NULL || out == NULL) {
        return E_POINTER;
    }

    *out = NULL;
    self = ds_api_downcast(com);
    dest = NULL;
    cli = NULL;

    src = ds_buffer_ref_checked(com_src);

    if (src == NULL) {
        trace("Attempted to duplicate alien buffer %p", com_src);
        hr = E_INVALIDARG;

        goto end;
    }

    hr = wasapi_snd_client_alloc(self->wasapi, &cli);

    if (FAILED(hr)) {
        goto end;
    }

    hr = ds_buffer_alloc(
            &dest,
            ds_buffer_unref_notify,
            ds_buffer_ref(src),
            cli,
            ds_buffer_get_snd_buffer(src),
            ds_buffer_get_format_(src),
            wasapi_get_sys_format(self->wasapi),
            ds_buffer_get_nbytes(src));

    if (FAILED(hr)) {
        goto end;
    }

    //trace("%p: Duped buffer from %p", dest, src);

    cli = NULL; /* dest has taken ownership */
    *out = ds_buffer_upcast(ds_buffer_ref(dest));

end:
    ds_buffer_unref(dest);
    snd_client_free(cli);
    ds_buffer_unref(src);

    return hr;
}

static __stdcall HRESULT ds_api_get_caps(
        IDirectSound8 *com,
        DSCAPS *caps)
{
    trace("%s [stub]", __func__);

    return E_NOTIMPL;
}

static __stdcall HRESULT ds_api_get_speaker_config(
        IDirectSound8 *com,
        DWORD *config)
{
    trace("%s(%p)", __func__, config);

    if (config == NULL) {
        return E_POINTER;
    }

    *config = DSSPEAKER_STEREO;

    return S_OK;
}

static __stdcall HRESULT ds_api_initialize(
        IDirectSound8 *com,
        const GUID *driver_id)
{
    trace("%s(%p)?", __func__, driver_id);

    return DSERR_ALREADYINITIALIZED;
}

static __stdcall HRESULT ds_api_set_cooperative_level(
        IDirectSound8 *com,
        HWND hwnd,
        DWORD level)
{
    trace("%s(%p, %08x)", __func__, hwnd, level);

    return S_OK;
}

static __stdcall HRESULT ds_api_set_speaker_config(
        IDirectSound8 *com,
        DWORD config)
{
    trace("%s(%08x)", __func__, config);

    return S_OK;
}

static __stdcall HRESULT ds_api_verify_certification(
        IDirectSound8 *com,
        DWORD *certified)
{
    /* ... pls */
    trace("%s(%p)", __func__);

    if (certified == NULL) {
        return E_POINTER;
    }

    *certified = DS_UNCERTIFIED;

    return S_OK;
}

static IDirectSound8Vtbl ds_api_vtbl = {
    .QueryInterface         = ds_api_query_interface,
    .AddRef                 = ds_api_add_ref,
    .Release                = ds_api_release,
    .Compact                = ds_api_compact,
    .CreateSoundBuffer      = ds_api_create_sound_buffer,
    .DuplicateSoundBuffer   = ds_api_duplicate_sound_buffer,
    .GetCaps                = ds_api_get_caps,
    .GetSpeakerConfig       = ds_api_get_speaker_config,
    .Initialize             = ds_api_initialize,
    .SetCooperativeLevel    = ds_api_set_cooperative_level,
    .SetSpeakerConfig       = ds_api_set_speaker_config,
    .VerifyCertification    = ds_api_verify_certification,
};

/* DirectSound API entry point, exported as DirectSoundCreate8 */

HRESULT __stdcall ds_api_create(
        const GUID *guid_device,
        IDirectSound8 **out,
        IUnknown *outer)
{
    struct ds_api *api;
    HRESULT hr;

    if (out == NULL) {
        return E_POINTER;
    }

    if (outer != NULL) {
        return E_NOTIMPL;
    }

    trace("Initializing Hypersonik: Allocating system resources");

    *out = NULL;
    hr = ds_api_alloc(&api);

    if (FAILED(hr)) {
        goto end;
    }

    trace("Initializing Hypersonik: Launching realtime audio thread");

    hr = ds_api_start(api);

    if (FAILED(hr)) {
        goto end;
    }

    *out = ds_api_upcast(ds_api_ref(api));

end:
    if (SUCCEEDED(hr)) {
        trace("Initializing Hypersonik: OK");
    } else {
        trace("Initializing Hypersonik: Failed! hr=%08x", hr);
    }

    ds_api_unref(api);

    return hr;
}
