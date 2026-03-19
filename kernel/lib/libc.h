#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>
#include <stdint.h>

/* Memory operations */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

/* String operations */
size_t strlen(const char* str);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* Conversion */
void itoa(int num, int base, char *buf);

/* Simple formatted output */
void printf(const char *format, ...);

#endif