#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "trace.h"

void trace_(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    tracev_(file, line, fmt, ap);
    va_end(ap);
}

void tracev_(const char *file, int line_no, const char *fmt, va_list ap)
{
    char msg[512];
    char line[512];
    int r;

    r = vsnprintf_s(msg, sizeof(msg), sizeof(msg) - 1, fmt, ap);

    if (r < 0) {
        abort();
    }

    r = _snprintf_s(
            line,
            sizeof(line),
            sizeof(line) - 1,
            "%s:%i: %s\n",
            file,
            line_no,
            msg);

    if (r < 0) {
        abort();
    }

    OutputDebugStringA(line);
}
