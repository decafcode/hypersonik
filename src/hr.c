#include <windows.h>

#include <errno.h>

#include "hr.h"
#include "trace.h"

HRESULT hr_from_errno(int r)
{
    if (r >= 0) {
        return S_OK;
    }

    switch (-r) {
    case EINVAL:    return E_INVALIDARG;
    case ENOMEM:    return E_OUTOFMEMORY;
    case ENOTSUP:   return E_NOTIMPL;
    default:        return E_FAIL;
    }
}

HRESULT hr_from_win32(void)
{
    return HRESULT_FROM_WIN32(GetLastError());
}

void hr_trace_(const char *file, int line, const char *func, HRESULT hr)
{
    trace_(file, line, "%s failed: hr=%08x", func, hr);
}
