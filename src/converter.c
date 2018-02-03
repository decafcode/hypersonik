#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "converter.h"
#include "trace.h"

struct converter {
    HACMSTREAM acm;
    ACMSTREAMHEADER header;
};

HRESULT converter_calculate_dest_nbytes(
        const WAVEFORMATEX *src,
        const WAVEFORMATEX *dest,
        size_t src_nbytes,
        size_t *out)
{
    uint64_t num;
    uint64_t den;

    assert(src != NULL);
    assert(dest != NULL);
    assert(out != NULL);

    *out = 0;

    if (    src->nSamplesPerSec == 0 ||
            src->nChannels == 0 ||
            src->wBitsPerSample == 0 ||
            src->wBitsPerSample % 8 != 0) {
        trace("Source format is invalid");

        return E_INVALIDARG;
    }

    if (    dest->nSamplesPerSec == 0 ||
            dest->nChannels == 0 ||
            dest->wBitsPerSample == 0 ||
            dest->wBitsPerSample % 8 != 0) {
        trace("Destination format is invalid");

        return E_INVALIDARG;
    }

    /*  Do an integer quotient rounding upwards here, because I am definitely
        not about to go calculating a buffer size using floating-point
        arithmetic. */

    num = src->nSamplesPerSec * src->nChannels * src->wBitsPerSample;
    den = dest->nSamplesPerSec * dest->nChannels * dest->wBitsPerSample;

    *out = (src_nbytes * num + (den - 1)) / den;

    return S_OK;
}

HRESULT converter_alloc(
        struct converter **out,
        const WAVEFORMATEX *src,
        const WAVEFORMATEX *dest,
        void *src_bytes,
        size_t src_nbytes,
        void *dest_bytes,
        size_t dest_nbytes)
{
    struct converter *conv;
    MMRESULT mmr;
    HRESULT hr;

    assert(src != NULL);
    assert(dest != NULL);
    assert(src_bytes != NULL);
    assert(dest_bytes != NULL);
    assert(out != NULL);

    *out = NULL;
    conv = NULL;

    conv = calloc(sizeof(*conv), 1);

    if (conv == NULL) {
        hr = E_OUTOFMEMORY;

        goto end;
    }

    /* This is an old API and its function signature is not const-correct */

    mmr = acmStreamOpen(
            &conv->acm,
            NULL,
            (WAVEFORMATEX *) src,
            (WAVEFORMATEX *) dest,
            NULL,
            0,
            0,
            ACM_STREAMOPENF_NONREALTIME);

    if (mmr != 0) {
        trace("acmStreamOpen failed: %i", mmr);
        hr = E_FAIL;

        goto end;
    }

    conv->header.cbStruct = sizeof(conv->header);
    conv->header.pbSrc = src_bytes;
    conv->header.cbSrcLength = src_nbytes;
    conv->header.pbDst = dest_bytes;
    conv->header.cbDstLength = dest_nbytes;

#if 0
    trace(  "ACMSTREAMHEADER:\n"
            "\tcbStruct = %i\n"
            "\tpbSrc = %p\n"
            "\tcbSrcLength = %i\n"
            "\tpbDst = %p\n"
            "\tcbDstLength = %i\n",
            conv->header.cbStruct,
            conv->header.pbSrc,
            conv->header.cbSrcLength,
            conv->header.pbDst,
            conv->header.cbDstLength);
#endif

    mmr = acmStreamPrepareHeader(conv->acm, &conv->header, 0);

    if (mmr != 0) {
        trace("acmStreamPrepareHeader failed: %i", mmr);
        hr = E_FAIL;

        goto end;
    }

    *out = conv;
    conv = NULL;
    hr = S_OK;

end:
    converter_free(conv);

    return hr;
}

void converter_free(struct converter *conv)
{
    MMRESULT mmr;

    if (conv == NULL) {
        return;
    }

    if (conv->header.fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED) {
        assert(conv->acm != NULL);

        mmr = acmStreamUnprepareHeader(conv->acm, &conv->header, 0);

        if (mmr != 0) {
            trace("acmStreamUnprepareHeader failed: %i", mmr);
        }
    }

    if (conv->acm != NULL) {
        mmr = acmStreamClose(conv->acm, 0);

        if (mmr != 0) {
            trace("acmStreamClose failed: %i", mmr);
        }
    }

    free(conv);
}

HRESULT converter_convert(
        struct converter *conv,
        size_t *src_nprocessed,
        size_t *dest_nprocessed)
{
    MMRESULT mmr;

    assert(conv != NULL);

    if (src_nprocessed != NULL) {
        *src_nprocessed = 0;
    }

    if (dest_nprocessed != NULL) {
        *dest_nprocessed = 0;
    }

    mmr = acmStreamConvert(conv->acm, &conv->header, 0);

    if (mmr != 0) {
        trace("acmStreamConvert failed: %i", mmr);

        return E_FAIL;
    }

    if (src_nprocessed != NULL) {
        *src_nprocessed = conv->header.cbSrcLengthUsed;
    }

    if (dest_nprocessed != NULL) {
        *dest_nprocessed = conv->header.cbDstLengthUsed;
    }

    return S_OK;
}
