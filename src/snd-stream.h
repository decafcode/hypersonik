#ifndef HYPERSONIK_SND_STREAM_H
#define HYPERSONIK_SND_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "list.h"
#include "snd-buffer.h"

struct snd_stream;

int snd_stream_alloc(struct snd_stream **out, const struct snd_buffer *buf);
void snd_stream_free(struct snd_stream *stm);
void snd_stream_set_looping(struct snd_stream *stm, bool value);
void snd_stream_set_volume(
        struct snd_stream *stm,
        size_t channel,
        uint16_t value);
size_t snd_stream_render(
        struct snd_stream *stm,
        int32_t *dest_samples,
        size_t dest_nsamples);
void snd_stream_rewind(struct snd_stream *stm);
bool snd_stream_is_finished(const struct snd_stream *stm);
size_t snd_stream_peek_position(const struct snd_stream *stm);
struct list_node *snd_stream_list_upcast(struct snd_stream *node);
struct snd_stream *snd_stream_list_downcast(struct list_node *node);

#endif
