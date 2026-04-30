#ifndef LINUX_SLAB_H
#define LINUX_SLAB_H

#include <stddef.h>

struct kmem_cache;

void kmem_cache_init(void);
struct kmem_cache *kmem_cache_create(size_t size);
void kmem_cache_destroy(struct kmem_cache *cache);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *ptr);

void *kmalloc(size_t size);
void kfree(const void *ptr);
void kheap_print_stats(void);

/* Minimal slab reclaim hook (Linux mapping: shrinkers / cache reaping). */
int slab_reclaim_one(void);

#endif
