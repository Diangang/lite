#ifndef KERNEL_H
#define KERNEL_H

#include <stddef.h>
#include <stdint.h>

/* I/O Port Helper Functions */
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

/* Terminal Helper Functions */
void terminal_initialize(void);
void terminal_writestring(const char* data);
void terminal_putchar(char c);

/* String Helpers */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

#endif
