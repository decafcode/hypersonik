#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "snd-mixer.h"
#include "snd-stream.h"

struct snd_mixer {
    struct list *streams;
    int32_t *work;
    size_t nsamples;
};

int snd_mixer_alloc(struct snd_mixer **out, size_t nframes, size_t nchannels)
{
    struct snd_mixer *m;
    int r;

    assert(out != NULL);
    assert(nframes > 0);

    *out = NULL;
    m = NULL;

    if (nchannels != 2) {
        r = -ENOTSUP;

        goto end;
    }

    m = calloc(sizeof(*m), 1);

    if (m == NULL) {
        r = -ENOMEM;

        goto end;
    }

    r = list_alloc(&m->streams);

    if (r < 0) {
        goto end;
    }

    m->nsamples = nframes * nchannels;
    m->work = malloc(m->nsamples * sizeof(int32_t));

    if (m->work == NULL) {
        r = -ENOMEM;

        goto end;
    }

    *out = m;
    m = NULL;

end:
    snd_mixer_free(m);

    return r;
}

void snd_mixer_free(struct snd_mixer *m)
{
    if (m == NULL) {
        return;
    }

    list_free(m->streams, NULL);
    free(m->work);
    free(m);
}

void snd_mixer_play(struct snd_mixer *m, struct snd_stream *stm)
{
    struct list_node *node;

    assert(m != NULL);
    assert(stm != NULL);

    snd_stream_rewind(stm);
    node = snd_stream_list_upcast(stm);

    if (!list_node_is_inserted(node)) {
        list_append(m->streams, node);
    }
}

void snd_mixer_stop(struct snd_mixer *m, struct snd_stream *stm)
{
    struct list_node *node;

    assert(m != NULL);
    assert(stm != NULL);

    node = snd_stream_list_upcast(stm);

    if (list_node_is_inserted(node)) {
        list_remove(m->streams, node);
    }
}

void snd_mixer_mix(struct snd_mixer *m, int16_t *samples)
{
    struct snd_stream *stm;
    struct list_node *node;
    struct list_iter i;
    int32_t sample;
    size_t j;

    assert(m != NULL);
    assert(samples != NULL);

    memset(m->work, 0, m->nsamples * sizeof(uint32_t));

    list_iter_init(&i, m->streams);

    while (list_iter_is_valid(&i)) {
        node = list_iter_deref(&i);
        stm = snd_stream_list_downcast(node);

        list_iter_next(&i);
        snd_stream_render(stm, m->work, m->nsamples);

        if (snd_stream_is_finished(stm)) {
            list_remove(m->streams, node);
        }
    }

    for (j = 0 ; j < m->nsamples ; j++) {
        sample = m->work[j] >> 8;

        if (sample < INT16_MIN) {
            samples[j] = INT16_MIN;
        } else if (sample > INT16_MAX) {
            samples[j] = INT16_MAX;
        } else {
            samples[j] = sample;
        }
    }
}
