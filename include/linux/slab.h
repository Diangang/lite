#ifndef LINUX_SLAB_H
#define LINUX_SLAB_H

#include <stddef.h>
#include "linux/gfp.h"
#include "linux/string.h"

struct kmem_cache;

void kmem_cache_init(void);
struct kmem_cache *kmem_cache_create(size_t size);
void kmem_cache_destroy(struct kmem_cache *cache);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *ptr);

void *kmalloc(size_t size);
static inline void *kmalloc_array(size_t n, size_t size, gfp_t flags)
{
    (void)flags;
    if (size != 0 && n > (size_t)-1 / size)
        return NULL;
    return kmalloc(n * size);
}
void kfree(const void *ptr);
size_t ksize(const void *ptr);
static inline void *kmem_cache_zalloc(struct kmem_cache *cache, gfp_t flags)
{
    void *ptr;

    (void)flags;
    ptr = kmem_cache_alloc(cache);
    if (ptr)
        memset(ptr, 0, ksize(ptr));
    return ptr;
}
static inline void *kzalloc(size_t size, gfp_t flags)
{
    void *ptr;

    (void)flags;
    ptr = kmalloc(size);
    if (ptr)
        memset(ptr, 0, ksize(ptr));
    return ptr;
}
void kheap_print_stats(void);

/* Minimal slab reclaim hook (Linux mapping: shrinkers / cache reaping). */
int slab_reclaim_one(void);

#endif
