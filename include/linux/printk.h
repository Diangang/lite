#ifndef LINUX_PRINTK_H
#define LINUX_PRINTK_H

#include <stdarg.h>

extern const char linux_banner[];

int printk(const char *format, ...);
int vprintk(const char *format, va_list args);
void printf(const char *format, ...);

#endif
