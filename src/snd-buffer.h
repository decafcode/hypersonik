#ifndef HYPERSONIK_SND_BUFFER_H
#define HYPERSONIK_SND_BUFFER_H

#include <stddef.h>
#include <stdint.h>

struct snd_buffer;

int snd_buffer_alloc(struct snd_buffer **out, size_t nsamples);
void snd_buffer_free(struct snd_buffer *buf);
const int16_t *snd_buffer_samples_ro(const struct snd_buffer *buf);
int16_t *snd_buffer_samples_rw(struct snd_buffer *buf);
size_t snd_buffer_nsamples(const struct snd_buffer *buf);

#endif
