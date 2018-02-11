#include <windows.h>
#include <dsound.h>

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "converter.h"
#include "defs.h"
#include "ds-buffer.h"
#include "hr.h"
#include "refcount.h"
#include "snd-buffer.h"
#include "snd-service.h"
#include "snd-stream.h"
#include "trace.h"

struct ds_buffer {
    IDirectSoundBuffer com;
    refcount_t rc;
    CRITICAL_SECTION lock; /* TODO implement locking */
    dtor_notify_t dtor_notify;
    void *dtor_notify_ctx;
    struct converter *conv;
    void *conv_bytes;
    size_t conv_nbytes;
    struct snd_buffer *buf;
    struct snd_stream *stm;
    struct snd_command *cmd_stop;
    struct snd_client *cli;
    HANDLE fence;
    WAVEFORMATEX format;
    WAVEFORMATEX format_sys;
    bool buf_owned;
    bool playing;
};

extern const GUID ds_buffer_private_iid;

static void ds_buffer_fence_signal(void *ptr);
static void ds_buffer_fence_wait(struct ds_buffer *self);
static bool ds_buffer_requires_conversion(const struct ds_buffer *self);
static HRESULT ds_buffer_prepare_conversion(struct ds_buffer *self);

static IDirectSoundBufferVtbl ds_buffer_vtbl;

HRESULT ds_buffer_alloc(
        struct ds_buffer **out,
        dtor_notify_t dtor_notify,
        void *dtor_notify_ctx,
        struct snd_client *cli,
        struct snd_buffer *buf,
        const WAVEFORMATEX *format,
        const WAVEFORMATEX *format_sys,
        size_t nbytes)
{
    struct ds_buffer *self;
    size_t sys_nbytes;
    HRESULT hr;
    int r;

    assert(out != NULL);
    assert(cli != NULL);
    assert(format != NULL);
    assert(format_sys != NULL);

    *out = NULL;
    self = NULL;

    if (format_sys->nChannels != 2 || format_sys->wBitsPerSample != 16) {
        trace("Unsupported system audio format");
        hr = E_NOTIMPL;

        return hr;
    }

    self = calloc(sizeof(*self), 1);

    if (self == NULL) {
        hr = E_OUTOFMEMORY;

        goto end;
    }

    self->com.lpVtbl = &ds_buffer_vtbl;
    self->rc = 1;
    memcpy(&self->format, format, sizeof(*format));
    memcpy(&self->format_sys, format_sys, sizeof(*format_sys));

    self->fence = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (self->fence == NULL) {
        hr = hr_from_win32();
        hr_trace("CreateEvent", hr);

        goto end;
    }

    self->conv_nbytes = nbytes;

    hr = converter_calculate_dest_nbytes(
            format,
            format_sys,
            nbytes,
            &sys_nbytes);

    if (FAILED(hr)) {
        goto end;
    }

    if (buf != NULL) {
        self->buf = buf;
    } else {
        r = snd_buffer_alloc(&self->buf, sys_nbytes / 2);

        if (r < 0) {
            hr = hr_from_errno(r);
            trace("snd_buffer_alloc failed: %i", r);

            goto end;
        }

        self->buf_owned = true;
    }

    r = snd_stream_alloc(&self->stm, self->buf);

    if (r < 0) {
        hr = hr_from_errno(r);
        trace("snd_stream_alloc failed: %i", r);

        goto end;
    }

    /* Pre-allocate a stop command to ensure that the destructor cannot fail */

    r = snd_client_cmd_alloc(cli, &self->cmd_stop);

    if (r < 0) {
        hr = hr_from_errno(r);
        trace("snd_client_cmd_alloc failed: %i", r);

        goto end;
    }

    snd_command_stop(self->cmd_stop, self->stm);
    snd_command_set_callback(self->cmd_stop, ds_buffer_fence_signal, self);

    /*  Commit to constructing this object: Take ownership of passed-in
        resources and store the destructor notification callback. */

    self->cli = cli;
    self->dtor_notify = dtor_notify;
    self->dtor_notify_ctx = dtor_notify_ctx;
    dtor_notify = NULL;

    *out = ds_buffer_ref(self);
    hr = S_OK;

end:
    ds_buffer_unref(self);

    if (dtor_notify != NULL) {
        dtor_notify(dtor_notify_ctx);
    }

    return hr;
}

struct ds_buffer *ds_buffer_downcast(IDirectSoundBuffer *com)
{
    if (com == NULL) {
        return NULL;
    }

    return containerof(com, struct ds_buffer, com);
}

IDirectSoundBuffer *ds_buffer_upcast(struct ds_buffer *self)
{
    if (self == NULL) {
        return NULL;
    }

    return &self->com;
}

struct ds_buffer *ds_buffer_ref(struct ds_buffer *self)
{
    assert(self != NULL);
    refcount_inc(&self->rc);

    return self;
}

struct ds_buffer *ds_buffer_ref_checked(IDirectSoundBuffer *com)
{
    IDirectSoundBuffer *checked;
    HRESULT hr;

    if (com == NULL) {
        return NULL;
    }

    hr = IDirectSoundBuffer_QueryInterface(
            com,
            &ds_buffer_private_iid,
            (void **) &checked);

    if (FAILED(hr)) {
        return NULL;
    }

    return ds_buffer_downcast(checked);
}

struct ds_buffer *ds_buffer_unref(struct ds_buffer *self)
{
    BOOL ok;

    if (self == NULL || refcount_dec(&self->rc) > 0) {
        return NULL;
    }

    free(self->conv_bytes);
    converter_free(self->conv);

    if (self->cli != NULL) {
        if (self->cmd_stop != NULL) {
            /*  First, send our allocated-ahead-of-time stop command and then
                wait for the fence to ensure that it has been processed. This
                ensures that the mixer is not reading from our stream, and it
                is safe for that stream to be deallocated. */

            assert(self->fence != NULL);

            snd_client_cmd_submit(self->cli, self->cmd_stop);
            ds_buffer_fence_wait(self);

            /*  (posting a command to a snd_client releases ownership, so we
                can consider self->cmd_stop to be destroyed here). */
        }

        snd_client_free(self->cli);
    }

    snd_stream_free(self->stm);

    /*  Note: we may or may not own the buffer that our stream was attached to;
        if this buffer was created via IDirectSound::DuplicateSoundBuffer then
        this will not execute. */

    if (self->buf_owned) {
        snd_buffer_free(self->buf);
    }

    if (self->fence != NULL) {
        ok = CloseHandle(self->fence);

        if (!ok) {
            hr_trace("CloseHandle(self->fence)", hr_from_win32());
        }
    }

    if (self->dtor_notify != NULL) {
        self->dtor_notify(self->dtor_notify_ctx);
    }

    trace("%p: Released buffer", self);
    free(self);

    return NULL;
}

void ds_buffer_unref_notify(void *ptr)
{
    ds_buffer_unref(ptr);
}

static void ds_buffer_fence_signal(void *ptr)
{
    struct ds_buffer *self;
    BOOL ok;

    self = ptr;
    ok = SetEvent(self->fence);

    if (!ok) {
        hr_trace("SetEvent(self->fence)", hr_from_win32());
    }
}

static void ds_buffer_fence_wait(struct ds_buffer *self)
{
    DWORD result;

    result = WaitForSingleObject(self->fence, INFINITE);

    if (result != WAIT_OBJECT_0) {
        hr_trace("WaitForSingleObject(self->fence)", hr_from_win32());
    }
}

struct snd_buffer *ds_buffer_get_snd_buffer(struct ds_buffer *self)
{
    assert(self != NULL);

    return self->buf;
}

const WAVEFORMATEX *ds_buffer_get_format_(const struct ds_buffer *self)
{
    assert(self != NULL);

    return &self->format;
}

size_t ds_buffer_get_nbytes(const struct ds_buffer *self)
{
    assert(self != NULL);

    return self->conv_nbytes;
}

static bool ds_buffer_requires_conversion(const struct ds_buffer *self)
{
    assert(self != NULL);

    return  self->format.nSamplesPerSec != self->format_sys.nSamplesPerSec ||
            self->format.nChannels != self->format_sys.nChannels ||
            self->format.wBitsPerSample != self->format_sys.wBitsPerSample;
}

static HRESULT ds_buffer_prepare_conversion(struct ds_buffer *self)
{
    HRESULT hr;

    assert(self != NULL);

    if (self->conv != NULL) {
        return S_FALSE;
    }

    assert(self->conv_bytes == NULL);

    self->conv_bytes = calloc(self->conv_nbytes, 1);

    if (self->conv_bytes == NULL) {
        hr = E_OUTOFMEMORY;

        goto fail;
    }

    hr = converter_alloc(
            &self->conv,
            &self->format,
            &self->format_sys,
            self->conv_bytes,
            self->conv_nbytes,
            snd_buffer_samples_rw(self->buf),
            snd_buffer_nbytes(self->buf));

    if (FAILED(hr)) {
        goto fail;
    }

    return S_OK;

fail:
    converter_free(self->conv);
    free(self->conv_bytes);

    return hr;
}

static __stdcall HRESULT ds_buffer_query_interface(
        IDirectSoundBuffer *com,
        const IID *iid,
        void **out)
{
    struct ds_buffer *self;

    if (iid == NULL || out == NULL) {
        return E_POINTER;
    }

    *out = NULL;
    self = ds_buffer_downcast(com);

    /*  We don't actually have an IDirectSoundBuffer8 vtbl.
        But, just say that we do for now. */

    if (    memcmp(iid, &ds_buffer_private_iid, sizeof(*iid)) == 0 ||
            memcmp(iid, &IID_IDirectSoundBuffer8, sizeof(*iid)) == 0 ||
            memcmp(iid, &IID_IDirectSoundBuffer, sizeof(*iid)) == 0 ||
            memcmp(iid, &IID_IUnknown, sizeof(*iid)) == 0) {
        ds_buffer_ref(self);
        *out = com;

        return S_OK;
    } else {
        return E_NOINTERFACE;
    }
}

static __stdcall ULONG ds_buffer_add_ref(IDirectSoundBuffer *com)
{
    ds_buffer_ref(ds_buffer_downcast(com));

    return 0;
}

static __stdcall ULONG ds_buffer_release(IDirectSoundBuffer *com)
{
    ds_buffer_unref(ds_buffer_downcast(com));

    return 0;
}

static __stdcall HRESULT ds_buffer_get_caps(
        IDirectSoundBuffer *com,
        DSBCAPS *out)
{
    trace("%s(%p) [stub]", __func__, out);

    return E_NOTIMPL;
}

static __stdcall HRESULT ds_buffer_get_current_position(
        IDirectSoundBuffer *com,
        DWORD *cur_play_byte_no,
        DWORD *cur_write_byte_no)
{
    struct ds_buffer *self;
    size_t sys_frame_pos;
    size_t sys_byte_pos;
    size_t tmp;
    HRESULT hr;

    self = ds_buffer_downcast(com);

    if (cur_play_byte_no != NULL) {
        sys_frame_pos = snd_stream_peek_position(self->stm);
        sys_byte_pos = sys_frame_pos
                * (self->format_sys.wBitsPerSample / 8)
                * self->format_sys.nChannels;

        hr = converter_calculate_dest_nbytes(
                &self->format_sys,
                &self->format,
                sys_byte_pos,
                &tmp);

        assert(SUCCEEDED(hr));

        *cur_play_byte_no = tmp;
    }

    if (cur_write_byte_no != NULL) {
        *cur_write_byte_no = 0;
    }

    return S_OK;
}

static __stdcall HRESULT ds_buffer_get_format(
        IDirectSoundBuffer *com,
        WAVEFORMATEX *out,
        DWORD nbytes,
        DWORD *nbytes_out)
{
    trace("%s(%p, %u, %p) [stub]", __func__, out, nbytes, nbytes_out);

    return E_NOTIMPL;
}

static __stdcall HRESULT ds_buffer_get_frequency(
        IDirectSoundBuffer *com,
        DWORD *out)
{
    struct ds_buffer *self;

    trace("%s(%p)", __func__, out);

    if (out == NULL) {
        return E_POINTER;
    }

    self = ds_buffer_downcast(com);
    *out = self->format.nSamplesPerSec;

    return S_OK;
}

static __stdcall HRESULT ds_buffer_get_pan(
        IDirectSoundBuffer *com,
        LONG *out)
{
    trace("%s(%p) [stub]", __func__, out);

    return E_NOTIMPL;
}

static __stdcall HRESULT ds_buffer_get_status(
        IDirectSoundBuffer *com,
        DWORD *out)
{
    struct ds_buffer *self;
    DWORD status;

    if (out == NULL) {
        return E_POINTER;
    }

    self = ds_buffer_downcast(com);

    /* Make sure self->playing is up to date */

    if (self->playing && snd_stream_is_finished(self->stm)) {
        self->playing = false;
    }

    status = 0;

    if (self->playing) {
        status |= DSBSTATUS_PLAYING;
    }

    if (snd_stream_get_looping(self->stm)) {
        status |= DSBSTATUS_LOOPING;
    }

    *out = status;

    return S_OK;
}

static __stdcall HRESULT ds_buffer_get_volume(
        IDirectSoundBuffer *com,
        LONG *out)
{
    trace("%s(%p)", __func__, out);

    return E_NOTIMPL;
}

static __stdcall HRESULT ds_buffer_initialize(
        IDirectSoundBuffer *com,
        IDirectSound *api,
        const DSBUFFERDESC *desc)
{
    trace("%s(%p, %p)?", __func__, api, desc);

    return E_UNEXPECTED;
}

static __stdcall HRESULT ds_buffer_lock(
        IDirectSoundBuffer *com,
        DWORD in_pos,
        DWORD in_nbytes,
        void **out_ptr,
        DWORD *out_nbytes,
        void **out_ptr2,
        DWORD *out_nbytes2,
        DWORD flags)
{
    struct ds_buffer *self;
    uint8_t *buf_bytes;
    size_t buf_nbytes;
    size_t span_start;
    size_t span_end;
    HRESULT hr;

    self = ds_buffer_downcast(com);

    /* Initial parameter validation */

    if (out_ptr == NULL || out_nbytes == NULL) {
        trace("%s: Main span out params are NULL", __func__);

        return E_POINTER;
    }

    *out_ptr = NULL;
    *out_nbytes = 0;

    if (out_ptr2 != NULL || out_nbytes2 != NULL) {
        trace("%s: Circular buffer lock is not implemented", __func__);

        return E_NOTIMPL;
    }

    if (flags & DSBLOCK_FROMWRITECURSOR) {
        trace("%s: Write cursor is not implemented", __func__);

        return E_NOTIMPL;
    }

    /* Acquire a suitable destination buffer */

    if (ds_buffer_requires_conversion(self)) {
        /*  Lazily allocate the conversion buffer. This might be a cloned
            buffer that never actually gets locked, after all. No sense in
            wasting time and memory. */

        hr = ds_buffer_prepare_conversion(self);

        if (FAILED(hr)) {
            return hr;
        }

        buf_bytes = (uint8_t *) self->conv_bytes;
        buf_nbytes = self->conv_nbytes;
    } else {
        buf_bytes = (uint8_t *) snd_buffer_samples_rw(self->buf);
        buf_nbytes = snd_buffer_nsamples(self->buf) * 2;
    }

    /* Decode args into a span */

    if (flags & DSBLOCK_ENTIREBUFFER) {
        span_start = 0;
        span_end = buf_nbytes;
    } else {
        span_start = in_pos;
        span_end = in_pos + in_nbytes;
    }

    /* Bounds check */

    if (span_start >= buf_nbytes) {
        trace("span_start %i >= buf_nbytes %i", span_start, buf_nbytes);
        span_start = buf_nbytes;
    }

    if (span_end > buf_nbytes) {
        trace("span_end %i > buf_nbytes %i", span_end, buf_nbytes);
        span_end = buf_nbytes;
    }

    /* Done */

    *out_ptr = buf_bytes;
    *out_nbytes = span_end - span_start;

    return S_OK;
}

static __stdcall HRESULT ds_buffer_play(
        IDirectSoundBuffer *com,
        DWORD reserved1,
        DWORD reserved2,
        DWORD flags)
{
    struct ds_buffer *self;
    struct snd_command *cmd;
    int r;

    self = ds_buffer_downcast(com);

    /*  Why only two reserved parameters? Why not ten?
        You know, just to be sure. Fucking Microsoft. */

    r = snd_client_cmd_alloc(self->cli, &cmd);

    if (r < 0) {
        return hr_from_errno(r);
    }

    snd_command_play(cmd, self->stm, flags & DSBPLAY_LOOPING);
    snd_client_cmd_submit(self->cli, cmd);

    return S_OK;
}

static __stdcall HRESULT ds_buffer_restore(IDirectSoundBuffer *com)
{
    trace("%s?", __func__);

    return S_OK;
}

static __stdcall HRESULT ds_buffer_set_current_position(
        IDirectSoundBuffer *com,
        DWORD pos)
{
    if (pos != 0) {
        trace("%s: erk, nonzero seek (%i)", pos);
    }

    return S_OK;
}

static __stdcall HRESULT ds_buffer_set_format(
        IDirectSoundBuffer *com,
        const WAVEFORMATEX *format)
{
    struct ds_buffer *self;

    trace("%s(%p) [stub]", __func__, format);

    if (format == NULL) {
        return E_POINTER;
    }

    self = ds_buffer_downcast(com);
    memcpy(&self->format, format, sizeof(*format));

    return S_OK;
}

static __stdcall HRESULT ds_buffer_set_frequency(
        IDirectSoundBuffer *com,
        DWORD freq)
{
    trace("%s(%u) [stub]", __func__, freq);

    return S_OK;
}

static __stdcall HRESULT ds_buffer_set_pan(
        IDirectSoundBuffer *com,
        LONG pan)
{
    // stub

    return S_OK;
}

static __stdcall HRESULT ds_buffer_set_volume(
        IDirectSoundBuffer *com,
        LONG millibels)
{
    struct ds_buffer *self;
    struct snd_command *cmd;
    int16_t linear_vol;
    int r;

    if (millibels < -10000 || millibels > 0) {
        trace("%s: Attenutation param out of range: %li", millibels);

        return E_INVALIDARG;
    }

    self = ds_buffer_downcast(com);
    linear_vol = 256.0 * pow(10.0, millibels / 2000.0);

    r = snd_client_cmd_alloc(self->cli, &cmd);

    if (r < 0) {
        return hr_from_errno(r);
    }

    snd_command_set_volume(cmd, self->stm, 0, linear_vol);
    snd_command_set_volume(cmd, self->stm, 1, linear_vol);
    snd_client_cmd_submit(self->cli, cmd);

    return S_OK;
}

static __stdcall HRESULT ds_buffer_stop(IDirectSoundBuffer *com)
{
    struct ds_buffer *self;
    struct snd_command *cmd;
    int r;

    self = ds_buffer_downcast(com);

    r = snd_client_cmd_alloc(self->cli, &cmd);

    if (r < 0) {
        return hr_from_errno(r);
    }

    snd_command_stop(cmd, self->stm);
    snd_client_cmd_submit(self->cli, cmd);

    return S_OK;
}

static __stdcall HRESULT ds_buffer_unlock(
        IDirectSoundBuffer *com,
        void *bytes,
        DWORD nbytes,
        void *bytes2,
        DWORD nbytes2)
{
    struct ds_buffer *self;

    self = ds_buffer_downcast(com);

    if (self->conv != NULL) {
        return converter_convert(self->conv, NULL, NULL);
    }

    return S_OK;
}

static struct IDirectSoundBufferVtbl ds_buffer_vtbl = {
    .QueryInterface     = ds_buffer_query_interface,
    .AddRef             = ds_buffer_add_ref,
    .Release            = ds_buffer_release,
    .GetCaps            = ds_buffer_get_caps,
    .GetCurrentPosition = ds_buffer_get_current_position,
    .GetFormat          = ds_buffer_get_format,
    .GetFrequency       = ds_buffer_get_frequency,
    .GetPan             = ds_buffer_get_pan,
    .GetStatus          = ds_buffer_get_status,
    .GetVolume          = ds_buffer_get_volume,
    .Initialize         = ds_buffer_initialize,
    .Lock               = ds_buffer_lock,
    .Play               = ds_buffer_play,
    .Restore            = ds_buffer_restore,
    .SetCurrentPosition = ds_buffer_set_current_position,
    .SetFormat          = ds_buffer_set_format,
    .SetFrequency       = ds_buffer_set_frequency,
    .SetPan             = ds_buffer_set_pan,
    .SetVolume          = ds_buffer_set_volume,
    .Stop               = ds_buffer_stop,
    .Unlock             = ds_buffer_unlock,
};
