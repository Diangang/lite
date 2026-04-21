#ifndef LINUX_KERNEL_H
#define LINUX_KERNEL_H

#include <stddef.h>
#include <stdarg.h>

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

__attribute__((noreturn))
void panic(const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *format, va_list args);
int snprintf(char *buf, size_t size, const char *format, ...);

#endif
