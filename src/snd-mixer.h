#ifndef HYPERSONIK_SND_MIXER_H
#define HYPERSONIK_SND_MIXER_H

#include <stddef.h>
#include <stdint.h>

#include "list.h"
#include "snd-stream.h"

struct snd_mixer;

int snd_mixer_alloc(struct snd_mixer **out, size_t nframes, size_t nchannels);
void snd_mixer_free(struct snd_mixer *m);
void snd_mixer_play(struct snd_mixer *m, struct snd_stream *stm);
void snd_mixer_stop(struct snd_mixer *m, struct snd_stream *stm);
void snd_mixer_mix(struct snd_mixer *m, int16_t *samples);

#endif