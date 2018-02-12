#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "defs.h"
#include "list.h"
#include "snd-buffer.h"

struct snd_stream {
    struct list_node node;
    const struct snd_buffer *buf;
    atomic_uint pos;
    uint16_t volumes[2];
    atomic_bool looping;
};

int snd_stream_alloc(struct snd_stream **out, const struct snd_buffer *buf)
{
    struct snd_stream *stm;

    assert(out != NULL);
    assert(buf != NULL);

    *out = NULL;
    stm = calloc(sizeof(*stm), 1);

    if (stm == NULL) {
        return -ENOMEM;
    }

    list_node_init(&stm->node);
    stm->buf = buf;
    stm->volumes[0] = 0x100;
    stm->volumes[1] = 0x100;

    *out = stm;

    return 0;
}

void snd_stream_free(struct snd_stream *stm)
{
    if (stm == NULL) {
        return;
    }

    list_node_fini(&stm->node);
    free(stm);
}

void snd_stream_set_looping(struct snd_stream *stm, bool value)
{
    assert(stm != NULL);

    atomic_store(&stm->looping, value);
}

void snd_stream_set_volume(
        struct snd_stream *stm,
        size_t channel,
        uint16_t value)
{
    assert(stm != NULL);
    assert(channel < lengthof(stm->volumes));

    stm->volumes[channel] = value;
}

void snd_stream_render(
        struct snd_stream *stm,
        int32_t *dest,
        size_t dest_nsamples)
{
    const int16_t *src;
    const int16_t *src_end;
    const int16_t *buf_samples;
    size_t buf_nsamples;
    size_t pos;
    size_t pos_end;

    assert(dest_nsamples % 2 == 0);

    buf_samples = snd_buffer_samples_ro(stm->buf);
    buf_nsamples = snd_buffer_nsamples(stm->buf);

    pos = stm->pos;

    for (;;) {
        pos_end = pos + dest_nsamples;

        if (pos_end > buf_nsamples) {
            pos_end = buf_nsamples;
            dest_nsamples -= buf_nsamples - pos;
        } else {
            dest_nsamples = 0;
        }

        src = &buf_samples[pos];
        src_end = &buf_samples[pos_end];

        while (src < src_end) {
            *dest++ += *src++ * stm->volumes[0];
            *dest++ += *src++ * stm->volumes[1];
        }

        pos = pos_end;

        if (dest_nsamples == 0 || !stm->looping) {
            break;
        }

        pos = 0;
    }

    atomic_store(&stm->pos, pos);
}

void snd_stream_rewind(struct snd_stream *stm)
{
    assert(stm != NULL);
    atomic_store(&stm->pos, 0);
}

bool snd_stream_is_finished(const struct snd_stream *stm)
{
    assert(stm != NULL);

    return  atomic_load(&stm->looping) == false &&
            atomic_load(&stm->pos) >= snd_buffer_nsamples(stm->buf);
}

size_t snd_stream_peek_position(const struct snd_stream *stm)
{
    assert(stm != NULL);

    /* Convert result from samples (not very meaningful) to frames */

    return atomic_load(&stm->pos) / 2;
}

struct list_node *snd_stream_list_upcast(struct snd_stream *stm)
{
    assert(stm != NULL);

    return &stm->node;
}

struct snd_stream *snd_stream_list_downcast(struct list_node *node)
{
    assert(node != NULL);

    return containerof(node, struct snd_stream, node);
}
