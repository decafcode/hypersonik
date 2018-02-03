#ifndef HYPERSONIK_CONVERTER_H
#define HYPERSONIK_CONVERTER_H

#include <winerror.h>
#include <mmreg.h>

#include <stddef.h>

struct converter;

HRESULT converter_calculate_dest_nbytes(
        const WAVEFORMATEX *src,
        const WAVEFORMATEX *dest,
        size_t src_nbytes,
        size_t *out);

HRESULT converter_alloc(
        struct converter **out,
        const WAVEFORMATEX *src,
        const WAVEFORMATEX *dest,
        void *src_bytes,
        size_t src_nbytes,
        void *dest_bytes,
        size_t dest_nbytes);

void converter_free(struct converter *conv);

HRESULT converter_convert(
        struct converter *conv,
        size_t *src_nprocessed,
        size_t *dest_nprocessed);

#endif
