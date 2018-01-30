#ifndef HYPERSONIK_DS_BUFFER_H
#define HYPERSONIK_DS_BUFFER_H

#include <windows.h>
#include <dsound.h>

#include "refcount.h"
#include "snd-buffer.h"
#include "snd-service.h"

struct ds_buffer;

HRESULT ds_buffer_alloc(
        struct ds_buffer **out,
        dtor_notify_t dtor_notify,
        void *dtor_notify_ctx,
        struct snd_client *cli,
        struct snd_buffer *buf,
        const WAVEFORMATEX *fmt,
        size_t nframes);
struct ds_buffer *ds_buffer_downcast(IDirectSoundBuffer *com);
IDirectSoundBuffer *ds_buffer_upcast(struct ds_buffer *self);
struct ds_buffer *ds_buffer_ref(struct ds_buffer *self);
struct ds_buffer *ds_buffer_ref_checked(IDirectSoundBuffer *com);
struct ds_buffer *ds_buffer_unref(struct ds_buffer *self);
void ds_buffer_unref_notify(void *ptr);
struct snd_buffer *ds_buffer_get_snd_buffer(struct ds_buffer *self);
const WAVEFORMATEX *ds_buffer_get_format_(const struct ds_buffer *self);

#endif
