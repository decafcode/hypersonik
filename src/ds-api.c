#include <windows.h>
#include <dsound.h>

/* DirectSound API entry point, exported as DirectSoundCreate8 */

HRESULT __stdcall ds_api_create(
        const GUID *guid_device,
        IDirectSound8 **out,
        IUnknown *outer)
{
    return E_NOTIMPL;
}
