#pragma once

#include <windows.h>

#include "snd-buffer.h"
#include "snd-service.h"
#include "snd-stream.h"

struct reaper;
struct reaper_task;

HRESULT reaper_alloc(
        struct reaper **reaper,
        struct snd_client *cli);

void reaper_free(struct reaper *reaper);

HRESULT reaper_start(struct reaper *reaper);

HRESULT reaper_alloc_task(
        struct reaper *reaper,
        struct reaper_task **task,
        struct snd_stream *stm,
        struct snd_buffer *buf);

void reaper_submit_task(struct reaper *reaper, struct reaper_task *task);

void reaper_task_discard(struct reaper_task *task);
