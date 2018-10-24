#pragma once

#include <stdarg.h>

#ifndef NDEBUG
#define trace(...) trace_(__FILE__, __LINE__, __VA_ARGS__)
#define tracev(fmt, ap) tracev_(__FILE__, __LINE__, fmt, ap)
#define trace_enter() trace(">>> %s", __func__)
#define trace_exit() trace("<<< %s", __func__)
#else
#define trace(...)
#define tracev(fmt, ap)
#define trace_enter()
#define trace_exit()
#endif

void trace_(const char *file, int line, const char *fmt, ...);
void tracev_(const char *file, int line, const char *fmt, va_list ap);
