#pragma once

#include <windows.h>
#include <dsound.h>

struct ds_buffer_pri;

HRESULT ds_buffer_pri_alloc(struct ds_buffer_pri **out);
struct ds_buffer_pri *ds_buffer_pri_downcast(IDirectSoundBuffer *com);
IDirectSoundBuffer *ds_buffer_pri_upcast(struct ds_buffer_pri *self);
struct ds_buffer_pri *ds_buffer_pri_ref(struct ds_buffer_pri *self);
struct ds_buffer_pri *ds_buffer_pri_unref(struct ds_buffer_pri *self);
