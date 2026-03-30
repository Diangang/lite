#include <stdarg.h>
#include "linux/libc.h"
#include "linux/console.h"
#include "linux/slab.h"
#include "linux/printk.h"

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *d = dest;
    const char *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;
    while ((*dest++ = *src++) != '\0')
        /* nothing */;
    return tmp;
}

int strcmp(const char *s1, const char *s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char *strcat(char *dest, const char *src) {
    char *rdest = dest;
    while (*dest)
        dest++;
    while (*src)
        *dest++ = *src++;
    *dest = '\0';
    return rdest;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* Integer to ASCII */
void itoa(int num, int base, char *buf)
{
    int i = 0;
    int is_negative = 0;
    unsigned int u_num;

    if (num == 0) {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }

    if (num < 0 && base == 10) {
        is_negative = 1;
        u_num = (unsigned int)-num;
    } else {
        u_num = (unsigned int)num;
    }

    while (u_num != 0) {
        int rem = u_num % base;
        buf[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        u_num = u_num / base;
    }

    if (is_negative)
        buf[i++] = '-';

    buf[i] = '\0';

    /* Reverse the string */
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buf[start];
        buf[start] = buf[end];
        buf[end] = temp;
        start++;
        end--;
    }
}

/* Simple printf implementation */
void printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintk(format, args);
    va_end(args);
}

char *strdup(const char *s)
{
    if (!s)
        return NULL;
    int len = strlen(s);
    char *dup = (char*)kmalloc(len + 1);
    if (dup)
        memcpy(dup, s, len + 1);
    return dup;
}
