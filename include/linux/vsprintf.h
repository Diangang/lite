#ifndef LINUX_VSPRINTF_H
#define LINUX_VSPRINTF_H

#include <stdarg.h>
#include <stddef.h>

int vsnprintf(char *buf, size_t size, const char *format, va_list args);
int snprintf(char *buf, size_t size, const char *format, ...);

#endif

