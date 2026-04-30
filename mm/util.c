#include "linux/slab.h"
#include "linux/string.h"

char *kstrdup(const char *s, gfp_t gfp)
{
    size_t len;
    char *buf;

    if (!s)
        return NULL;

    (void)gfp;
    len = strlen(s) + 1;
    buf = kmalloc(len);
    if (buf)
        memcpy(buf, s, len);
    return buf;
}

char *kstrndup(const char *s, size_t max, gfp_t gfp)
{
    size_t len = 0;
    char *buf;

    if (!s)
        return NULL;

    while (len < max && s[len])
        len++;

    (void)gfp;
    buf = kmalloc(len + 1);
    if (buf) {
        memcpy(buf, s, len);
        buf[len] = '\0';
    }
    return buf;
}

void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
    void *p;

    (void)gfp;
    p = kmalloc(len);
    if (p)
        memcpy(p, src, len);
    return p;
}
