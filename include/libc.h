#ifndef LIBC_H
#define LIBC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* I/O Port Helper Functions (moved from kernel.h to be accessible globally) */
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* String/Memory operations */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
char *strcpy(char *dest, const char *src);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* String operations */
size_t strlen(const char* str);
char *strcpy(char *dest, const char *src);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strdup(const char *s);

/* Conversion */
void itoa(int num, int base, char *buf);

/* Simple formatted output */
void printf(const char *format, ...);

__attribute__((noreturn))
static inline void panic(const char *msg)
{
    if (msg)
        printf("HALT: %s\n", msg);

    __asm__ volatile ("cli");

    for (;;)
        __asm__ volatile ("hlt");

    __builtin_unreachable();
}

#endif