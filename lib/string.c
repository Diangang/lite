#include "linux/string.h"
#include "linux/slab.h"
#include "linux/kernel.h"

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
        *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (!n || d == s)
        return dest;

    if (d < s) {
        while (n--)
            *d++ = *s++;
        return dest;
    }

    d += n;
    s += n;
    while (n--)
        *(--d) = *(--s);
    return dest;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str && str[len])
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

int strcmp(const char *s1, const char *s2)
{
    if (!s1)
        s1 = "";
    if (!s2)
        s2 = "";
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strcat(char *dest, const char *src)
{
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
    if (!s1)
        s1 = "";
    if (!s2)
        s2 = "";
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void itoa(int num, int base, char *buf)
{
    int i = 0;
    int is_negative = 0;
    unsigned int u_num;

    if (!buf)
        return;

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
        int rem = (int)(u_num % (unsigned int)base);
        buf[i++] = (char)((rem > 9) ? (rem - 10) + 'a' : rem + '0');
        u_num = u_num / (unsigned int)base;
    }

    if (is_negative)
        buf[i++] = '-';

    buf[i] = '\0';

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

char *strdup(const char *s)
{
    return kstrdup(s, GFP_KERNEL);
}
