#include <windows.h>

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <objbase.h>
#include <process.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "defs.h"
#include "hr.h"
#include "snd-mixer.h"
#include "snd-service.h"
#include "trace.h"
#include "wasapi.h"

struct wasapi {
    HANDLE thread;
    HANDLE started;
    HANDLE stop;
    struct snd_service *svc;
};

static unsigned int __stdcall wasapi_thread_main(void *ctx);
static HRESULT wasapi_thread_do_setup(
        IAudioClient **ac_out,
        IAudioRenderClient **rc_out,
        size_t *nframes_out,
        HANDLE event);
static HRESULT wasapi_renegotiate_buffer(
        IMMDevice *dev,
        IAudioClient **ac_ref,
        const WAVEFORMATEX *wfx);

static const WAVEFORMATEX wasapi_sys_wfx = {
    .wFormatTag         = WAVE_FORMAT_PCM,
    .nChannels          = 2,
    .nSamplesPerSec     = 44100,
    .wBitsPerSample     = 16,
    .nBlockAlign        = 4,
    .nAvgBytesPerSec    = 176400,
    .cbSize             = 0,
};

HRESULT wasapi_alloc(struct wasapi **out)
{
    struct wasapi *wasapi;
    HRESULT hr;
    int r;

    trace_enter();
    assert(out != NULL);

    *out = NULL;
    wasapi = calloc(sizeof(*wasapi), 1);

    if (wasapi == NULL) {
        hr = E_OUTOFMEMORY;

        goto end;
    }

    wasapi->started = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (wasapi->started == NULL) {
        hr = hr_from_win32();
        hr_trace("CreateEvent", hr);

        goto end;
    }

    wasapi->stop = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (wasapi->stop == NULL) {
        hr = hr_from_win32();
        hr_trace("CreateEvent", hr);

        goto end;
    }

    r = snd_service_alloc(&wasapi->svc);

    if (r < 0) {
        hr = hr_from_errno(r);

        goto end;
    }

    *out = wasapi;
    wasapi = NULL;
    hr = S_OK;

end:
    wasapi_free(wasapi);
    trace_exit();

    return hr;
}

void wasapi_free(struct wasapi *wasapi)
{
    HRESULT hr;
    BOOL ok;

    if (wasapi == NULL) {
        return;
    }

    hr = wasapi_stop(wasapi);

    if (FAILED(hr)) {
        trace("wasapi_stop failed! We're probably going to crash now.");
    }

    snd_service_free(wasapi->svc);

    if (wasapi->stop != NULL) {
        ok = CloseHandle(wasapi->stop);

        if (!ok) {
            hr_trace("CloseHandle(wasapi->stop)", hr_from_win32());
        }
    }

    if (wasapi->started != NULL) {
        ok = CloseHandle(wasapi->started);

        if (!ok) {
            hr_trace("CloseHandle(wasapi->started)", hr_from_win32());
        }
    }

    if (wasapi->thread != NULL) {
        ok = CloseHandle(wasapi->thread);

        if (!ok) {
            hr_trace("CloseHandle(wasapi->thread)", hr_from_win32());
        }
    }
}

HRESULT wasapi_start(struct wasapi *wasapi)
{
    HANDLE thread;
    HANDLE handles[2];
    HRESULT hr;
    BOOL ok;
    uint32_t wait;

    assert(wasapi != NULL);
    assert(wasapi->thread == NULL);

    thread = (HANDLE) _beginthreadex(
            NULL,
            0,
            wasapi_thread_main,
            wasapi,
            0,
            NULL);

    if (thread == NULL) {
        hr = hr_from_win32();
        hr_trace("_beginthreadex", hr);

        goto end;
    }

    handles[0] = wasapi->started;
    handles[1] = thread;

    wait = WaitForMultipleObjects(
            lengthof(handles),
            handles,
            FALSE,
            INFINITE);

    if (wait == 0) {
        /* Thread startup OK */
    } else if (wait == 1) {
        /* Thread exited prematurely */
        trace("WASAPI thread exited unexpectedly");

        ok = GetExitCodeThread(thread, (DWORD *) &hr);

        if (!ok) {
            hr = hr_from_win32();
            hr_trace("GetExitCodeThread", hr);
        } else {
            trace("WASAPI thread returned HRESULT %08x", hr);
        }

        goto end;
    } else {
        hr = hr_from_win32();
        hr_trace("WaitForMultipleObjects", hr);

        goto end;
    }

    wasapi->thread = thread;
    thread = NULL;
    hr = S_OK;

end:
    if (thread != NULL) {
        CloseHandle(thread);
    }

    return hr;
}

HRESULT wasapi_snd_client_alloc(
        struct wasapi *wasapi,
        struct snd_client **out)
{
    int r;

    assert(wasapi != NULL);

    r = snd_client_alloc(out, wasapi->svc);

    return hr_from_errno(r);
}

const WAVEFORMATEX *wasapi_get_sys_format(const struct wasapi *wasapi)
{
    assert(wasapi != NULL);

    return &wasapi_sys_wfx;
}

HRESULT wasapi_stop(struct wasapi *wasapi)
{
    uint32_t wait;
    HRESULT hr;
    BOOL ok;

    assert(wasapi != NULL);

    if (wasapi->thread == NULL) {
        return S_FALSE;
    }

    ok = SetEvent(wasapi->stop);

    if (!ok) {
        hr = hr_from_win32();
        hr_trace("SetEvent(wasapi->stop)", hr);

        goto end;
    }

    wait = WaitForSingleObject(wasapi->thread, INFINITE);

    if (wait != WAIT_OBJECT_0) {
        hr = hr_from_win32();
        hr_trace("WaitForSingleObject(wasapi->thread)", hr);

        goto end;
    }

    CloseHandle(wasapi->thread);
    wasapi->thread = NULL;
    hr = S_OK;

end:
    return hr;
}

static unsigned int __stdcall wasapi_thread_main(void *ctx)
{
    struct wasapi *wasapi;
    struct snd_mixer *mixer;
    void *frames;
    size_t nframes;
    IAudioClient *ac;
    IAudioRenderClient *rc;
    HANDLE events[2];
    HANDLE task;
    DWORD task_index;
    uint32_t wait_result;
    BOOL ok;
    HRESULT hr;
    int r;

    trace("WASAPI thread starting up");

    wasapi = ctx;
    mixer = NULL;
    ac = NULL;
    rc = NULL;
    events[0] = NULL;
    events[1] = NULL;
    task = NULL;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (FAILED(hr)) {
        hr_trace("CoInitializeEx", hr);

        goto end;
    }

    events[0] = wasapi->stop;
    events[1] = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (events[1] == NULL) {
        hr = hr_from_win32();
        hr_trace("WASAPI signal CreateEvent", hr);

        goto end;
    }

    hr = wasapi_thread_do_setup(&ac, &rc, &nframes, events[1]);

    if (FAILED(hr)) {
        goto end;
    }

    r = snd_mixer_alloc(&mixer, nframes, 2);

    if (r < 0) {
        trace("snd_mixer_alloc() failed: r = %i", r);
        hr = hr_from_errno(r);

        goto end;
    }

    ok = SetEvent(wasapi->started);

    if (!ok) {
        hr = hr_from_win32();
        hr_trace("SetEvent(wasapi->started)", hr);

        goto end;
    }

    trace("About to boost WASAPI thread and cease trace output");

    task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);

    if (task == NULL) {
        hr_trace("AvSetMmThreadCharacteristicsW", hr_from_win32());
    }

    hr = IAudioClient_Start(ac);

    if (FAILED(hr)) {
        hr_trace("IAudioClient::Start", hr);

        goto end;
    }

    for (;;) {
        wait_result = WaitForMultipleObjects(
                lengthof(events),
                events,
                FALSE,
                INFINITE);

        if (wait_result == 0) {
            /* Thread was commanded to stop */

            break;
        } else if (wait_result == 1) {
            /* DMA buffer is available */

            hr = IAudioRenderClient_GetBuffer(
                    rc,
                    nframes,
                    (BYTE **) &frames);

            if (FAILED(hr)) {
                hr_trace("IAudioRenderClient::GetBuffer", hr);

                break;
            }

            /* --- BEGIN APPLICATION LOGIC --- */

            snd_service_intake(wasapi->svc, mixer);
            snd_mixer_mix(mixer, frames);
            snd_service_exhaust(wasapi->svc);

            /* --- END APPLICATION LOGIC --- */

            hr = IAudioRenderClient_ReleaseBuffer(
                    rc,
                    nframes,
                    0);

            if (FAILED(hr)) {
                hr_trace("IAudioRenderClient::ReleaseBuffer", hr);

                break;
            }
        } else {
            /* Something went wrong */

            hr = hr_from_win32();
            hr_trace("WaitForMultipleObjects", hr);

            break;
        }
    }

    IAudioClient_Stop(ac);

end:
    if (task != NULL) {
        AvRevertMmThreadCharacteristics(task);
        trace("De-boosted WASAPI thread");
    }

    snd_mixer_free(mixer);

    if (events[1] != NULL) {
        ok = CloseHandle(events[1]);

        if (!ok) {
            hr_trace("CloseHandle(WASAPI Event)", hr_from_win32());
        }
    }

    if (rc != NULL) {
        IAudioRenderClient_Release(rc);
    }

    if (ac != NULL) {
        IAudioClient_Release(ac);
    }

    CoUninitialize();

    trace("WASAPI thread is exiting. hr = %08x", hr);

    return hr;
}

static HRESULT wasapi_thread_do_setup(
        IAudioClient **ac_out,
        IAudioRenderClient **rc_out,
        size_t *nframes_out,
        HANDLE event)
{
    REFERENCE_TIME period;
    IMMDeviceEnumerator *mmde;
    IMMDevice *dev;
    IAudioClient *ac;
    IAudioRenderClient *rc;
    UINT32 nframes;
    void *frames;
    HRESULT hr;

    trace("Searching for audio output device");

    *ac_out = NULL;
    *rc_out = NULL;
    *nframes_out = 0;

    mmde = NULL;
    dev = NULL;
    ac = NULL;
    rc = NULL;

    hr = CoCreateInstance(
            &CLSID_MMDeviceEnumerator,
            NULL,
            CLSCTX_ALL,
            &IID_IMMDeviceEnumerator,
            (void **) &mmde);

    if (FAILED(hr)) {
        hr_trace("CoCreateInstance(CLSID_MMDeviceEnumerator)", hr);

        goto end;
    }

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(
            mmde,
            eRender,
            eConsole,
            &dev);

    if (FAILED(hr)) {
        hr_trace("IMMDeviceEnumerator::GetDefaultAudioEndpoint", hr);

        goto end;
    }

    trace("Performing WASAPI startup bureaucracy");

    hr = IMMDevice_Activate(
            dev,
            &IID_IAudioClient,
            CLSCTX_ALL,
            NULL,
            (void **) &ac);

    if (FAILED(hr)) {
        hr_trace("IMMDevice::Activate", hr);

        goto end;
    }

    period = 0;
    hr = IAudioClient_GetDevicePeriod(
            ac,
            NULL,
            &period);

    if (FAILED(hr)) {
        hr_trace("IAudioClient::GetDevicePeriod", hr);

        goto end;
    }

    hr = IAudioClient_Initialize(
            ac,
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            period,
            period,
            &wasapi_sys_wfx,
            NULL);

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        hr = wasapi_renegotiate_buffer(dev, &ac, &wasapi_sys_wfx);
    }

    if (FAILED(hr)) {
        hr_trace("IAudioClient::Initialize", hr);

        goto end;
    }

    hr = IAudioClient_SetEventHandle(
            ac,
            event);

    if (FAILED(hr)) {
        hr_trace("IAudioClient::SetEventHandle", hr);

        goto end;
    }

    hr = IAudioClient_GetBufferSize(
            ac,
            &nframes);

    if (FAILED(hr)) {
        hr_trace("IAudioClient::GetBufferSize", hr);

        goto end;
    }

    trace(  "Negotiated mixing latency of %i frames (%f sec)",
            (int) nframes,
            nframes / (double) wasapi_sys_wfx.nSamplesPerSec);

    hr = IAudioClient_GetService(
            ac,
            &IID_IAudioRenderClient,
            (void **) &rc);

    if (FAILED(hr)) {
        hr_trace("IAudioClient::GetService(IID_IAudioRenderClient)", hr);

        goto end;
    }

    trace("Pre-rolling silence period");

    hr = IAudioRenderClient_GetBuffer(
            rc,
            nframes,
            (BYTE **) &frames);

    if (FAILED(hr)) {
        hr_trace("Preroll IAudioRenderClient::GetBuffer", hr);

        goto end;
    }

    hr = IAudioRenderClient_ReleaseBuffer(
            rc,
            nframes,
            AUDCLNT_BUFFERFLAGS_SILENT);

    if (FAILED(hr)) {
        hr_trace("Preroll IAudioRenderClient::ReleaseBuffer", hr);

        goto end;
    }

    IAudioClient_AddRef(ac);
    IAudioRenderClient_AddRef(rc);

    *ac_out = ac;
    *rc_out = rc;
    *nframes_out = nframes;

end:
    if (rc != NULL) {
        IAudioRenderClient_Release(rc);
    }

    if (ac != NULL) {
        IAudioClient_Release(ac);
    }

    if (dev != NULL) {
        IMMDevice_Release(dev);
    }

    if (mmde != NULL) {
        IMMDeviceEnumerator_Release(mmde);
    }

    return hr;
}

static HRESULT wasapi_renegotiate_buffer(
        IMMDevice *dev,
        IAudioClient **ac_ref,
        const WAVEFORMATEX *wfx)
{
    // https://blogs.msdn.microsoft.com/matthew_van_eerde/2009/04/03/sample-wasapi-exclusive-mode-event-driven-playback-app-including-the-hd-audio-alignment-dance/
    // Microsoft's API design is certainly ... something.

    IAudioClient *ac;
    REFERENCE_TIME period;
    UINT32 nframes;
    HRESULT hr;

    ac = *ac_ref;
    hr = IAudioClient_GetBufferSize(
            ac,
            &nframes);

    if (FAILED(hr)) {
        hr_trace("IAudioClient::GetBufferSize", hr);

        return hr;
    }

    IAudioClient_Release(ac);
    *ac_ref = NULL;

    period = (REFERENCE_TIME) (
            10000.0 *           // (hns / ms) *
            1000 *              // (ms / s) *
            nframes /           // frames /
            wfx->nSamplesPerSec // (frames / s)
            + 0.5               // rounding
    );

    trace("WASAPI tentative buffer size was %i frames", nframes);
    trace("Need a 'period' of %i usec", (int) (period / 10));

    hr = IMMDevice_Activate(
            dev,
            &IID_IAudioClient,
            CLSCTX_ALL,
            NULL,
            (void **) &ac);

    if (FAILED(hr)) {
        hr_trace("IMMDevice::Activate", hr);

        return hr;
    }

    *ac_ref = ac;

    return IAudioClient_Initialize(
            ac,
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            period,
            period,
            wfx,
            NULL);
}
