#ifndef LINUX_PRINTK_H
#define LINUX_PRINTK_H

#include <stdarg.h>

int printk(const char *format, ...);
int vprintk(const char *format, va_list args);

#endif
