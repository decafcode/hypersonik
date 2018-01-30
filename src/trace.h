#ifndef HYPERSONIK_TRACE_H
#define HYPERSONIK_TRACE_H

#include <stdarg.h>

#define trace(...) trace_(__FILE__, __LINE__, __VA_ARGS__)
#define tracev(fmt, ap) tracev_(__FILE__, __LINE__, fmt, ap)
#define trace_enter() trace(">>> %s", __func__)
#define trace_exit() trace("<<< %s", __func__)

void trace_(const char *file, int line, const char *fmt, ...);
void tracev_(const char *file, int line, const char *fmt, va_list ap);

#endif
