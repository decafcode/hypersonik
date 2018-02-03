#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "snd-buffer.h"

struct snd_buffer {
    int16_t *samples;
    size_t nsamples;
};

int snd_buffer_alloc(struct snd_buffer **out, size_t nsamples)
{
    struct snd_buffer *buf;
    int r;

    assert(out != NULL);
    assert(nsamples > 0);

    *out = NULL;
    buf = NULL;

    if (nsamples % 2 != 0) {
        r = -ENOTSUP;

        goto end;
    }

    buf = calloc(sizeof(*buf), 1);

    if (buf == NULL) {
        r = -ENOMEM;

        goto end;
    }

    buf->nsamples = nsamples;
    buf->samples = calloc(nsamples, sizeof(int16_t));

    if (buf->samples == NULL) {
        r = -ENOMEM;

        goto end;
    }

    *out = buf;
    buf = NULL;
    r = 0;

end:
    snd_buffer_free(buf);

    return r;
}

void snd_buffer_free(struct snd_buffer *buf)
{
    if (buf == NULL) {
        return;
    }

    free(buf->samples);
    free(buf);
}

const int16_t *snd_buffer_samples_ro(const struct snd_buffer *buf)
{
    assert(buf != NULL);

    return buf->samples;
}

int16_t *snd_buffer_samples_rw(struct snd_buffer *buf)
{
    assert(buf != NULL);

    return buf->samples;
}

size_t snd_buffer_nsamples(const struct snd_buffer *buf)
{
    assert(buf != NULL);

    return buf->nsamples;
}

size_t snd_buffer_nbytes(const struct snd_buffer *buf)
{
    assert(buf != NULL);

    return buf->nsamples * 2;
}
