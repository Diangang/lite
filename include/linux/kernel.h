#ifndef LINUX_KERNEL_H
#define LINUX_KERNEL_H

#include <stddef.h>

#define container_of(ptr, type, member) ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif
