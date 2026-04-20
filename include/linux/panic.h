#ifndef LINUX_PANIC_H
#define LINUX_PANIC_H

#include <stdarg.h>

__attribute__((noreturn))
void panic(const char *fmt, ...);

#endif
