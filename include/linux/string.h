#ifndef LINUX_STRING_H
#define LINUX_STRING_H

#include <stddef.h>
#include "linux/gfp.h"

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *kstrdup(const char *s);
void *kmemdup(const void *src, size_t len, gfp_t gfp);
char *strdup(const char *s);
void itoa(int num, int base, char *buf);

#endif
