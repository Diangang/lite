#include "linux/slab.h"
#include "asm/page.h"
#include "linux/page_alloc.h"
#include "linux/libc.h"
#include "linux/memlayout.h"

#define SLAB_MAX_PAGES 4096
#define SLAB_MAX_CACHE 9
#define SLAB_MAGIC 0x534C4142
#define LARGE_MAGIC 0x4C415247

struct slab {
    void *freelist;
    uint32_t inuse;
    uint32_t total;
    void *vaddr;
    struct slab *next;
    struct kmem_cache *cache;
};

struct kmem_cache {
    size_t size;
    struct slab *slabs;
};

struct large_hdr {
    uint32_t magic;
    uint32_t order;
    uint32_t phys;
    uint32_t pad;
};

static const size_t cache_sizes[SLAB_MAX_CACHE] = { 8, 16, 32, 64, 128, 256, 512, 1024, 2048 };
static struct kmem_cache caches[SLAB_MAX_CACHE];
static struct slab slab_pool[SLAB_MAX_PAGES];
static struct slab *slab_free_list = NULL;
static struct slab *slab_map[SLAB_MAX_PAGES];
static uint32_t slab_pages = 0;

static void slab_free_meta(struct slab *s)
{
    if (!s)
        return;
    s->next = slab_free_list;
    slab_free_list = s;
}

/* align_up: Implement align up. */
static size_t align_up(size_t value, size_t align)
{
    return (value + align - 1) & ~(align - 1);
}

/* slab_alloc_meta: Implement slab alloc meta. */
static struct slab *slab_alloc_meta(void)
{
    if (!slab_free_list)
        return NULL;
    struct slab *s = slab_free_list;
    slab_free_list = slab_free_list->next;
    memset(s, 0, sizeof(*s));
    return s;
}

/* slab_alloc_page: Implement slab alloc page. */
static void *slab_alloc_page(struct slab *s)
{
    if (slab_pages >= SLAB_MAX_PAGES)
        return NULL;
    void *phys = alloc_page(GFP_KERNEL);
    if (!phys)
        return NULL;
    uint32_t vaddr = (uint32_t)memlayout_directmap_phys_to_virt((uint32_t)phys);
    s->vaddr = (void*)vaddr;
    slab_map[slab_pages] = s;
    slab_pages++;
    return (void*)vaddr;
}

/* slab_from_ptr: Implement slab from ptr. */
static struct slab *slab_from_ptr(void *ptr)
{
    if (!ptr)
        return NULL;
    uint32_t page_base = ((uint32_t)ptr) & ~(PAGE_SIZE - 1);
    for (uint32_t i = 0; i < slab_pages; i++) {
        struct slab *s = slab_map[i];
        if (s && (uint32_t)s->vaddr == page_base)
            return s;
    }
    return NULL;
}

/* slab_init_objects: Implement slab init objects. */
static void slab_init_objects(struct slab *s, size_t size)
{
    uint8_t *base = (uint8_t*)s->vaddr;
    uint32_t total = PAGE_SIZE / size;
    s->total = total;
    s->inuse = 0;
    s->freelist = NULL;
    for (uint32_t i = 0; i < total; i++) {
        void *obj = base + i * size;
        *(void**)obj = s->freelist;
        s->freelist = obj;
    }
}

/* kmem_cache_create: Implement kmem cache create. */
struct kmem_cache *kmem_cache_create(size_t size)
{
    struct kmem_cache *cache = (struct kmem_cache*)kmalloc(sizeof(*cache));
    if (!cache)
        return NULL;
    cache->size = align_up(size, 8);
    if (cache->size < sizeof(void*))
        cache->size = sizeof(void*);
    cache->slabs = NULL;
    return cache;
}

/* kmem_cache_destroy: Implement kmem cache destroy. */
void kmem_cache_destroy(struct kmem_cache *cache)
{
    (void)cache;
}

/* kmem_cache_alloc: Implement kmem cache alloc. */
void *kmem_cache_alloc(struct kmem_cache *cache)
{
    if (!cache)
        return NULL;
    struct slab *s = cache->slabs;
    while (s && !s->freelist)
        s = s->next;
    if (!s) {
        s = slab_alloc_meta();
        if (!s)
            return NULL;
        s->cache = cache;
        if (!slab_alloc_page(s))
            return NULL;
        slab_init_objects(s, cache->size);
        s->next = cache->slabs;
        cache->slabs = s;
    }
    void *obj = s->freelist;
    if (!obj)
        return NULL;
    s->freelist = *(void**)obj;
    s->inuse++;
    return obj;
}

/* kmem_cache_free: Implement kmem cache free. */
void kmem_cache_free(struct kmem_cache *cache, void *ptr)
{
    if (!cache || !ptr)
        return;
    struct slab *s = slab_from_ptr(ptr);
    if (!s || s->cache != cache)
        return;
    *(void**)ptr = s->freelist;
    s->freelist = ptr;
    if (s->inuse)
        s->inuse--;
}

/* size_to_order: Implement size to order. */
static unsigned int size_to_order(size_t size)
{
    unsigned int order = 0;
    size_t bytes = PAGE_SIZE;
    while (bytes < size) {
        bytes <<= 1;
        order++;
    }
    return order;
}

/* kmalloc_large: Implement kmalloc large. */
static void *kmalloc_large(size_t size)
{
    size_t total = size + sizeof(struct large_hdr);
    unsigned int order = size_to_order(total);
    void *phys = alloc_pages(GFP_KERNEL, order);
    if (!phys)
        return NULL;
    uint32_t vaddr = (uint32_t)memlayout_directmap_phys_to_virt((uint32_t)phys);
    struct large_hdr *hdr = (struct large_hdr*)vaddr;
    hdr->magic = LARGE_MAGIC;
    hdr->order = order;
    hdr->phys = (uint32_t)phys;
    return (void*)(vaddr + sizeof(*hdr));
}

/* kmalloc: Implement kmalloc. */
void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;
    for (int i = 0; i < SLAB_MAX_CACHE; i++) {
        if (size <= cache_sizes[i])
            return kmem_cache_alloc(&caches[i]);
    }
    return kmalloc_large(size);
}

/* kfree: Implement kfree. */
void kfree(void *ptr)
{
    if (!ptr)
        return;
    struct slab *s = slab_from_ptr(ptr);
    if (s && s->cache) {
        kmem_cache_free(s->cache, ptr);
        return;
    }
    struct large_hdr *hdr = (struct large_hdr*)((uint32_t)ptr - sizeof(struct large_hdr));
    if (hdr->magic != LARGE_MAGIC)
        return;
    free_pages(hdr->phys, hdr->order);
}

/* kheap_print_stats: Implement kheap print stats. */
void kheap_print_stats(void)
{
    for (int i = 0; i < SLAB_MAX_CACHE; i++) {
        struct kmem_cache *c = &caches[i];
        uint32_t slabs = 0;
        uint32_t free_objs = 0;
        uint32_t total_objs = 0;
        struct slab *s = c->slabs;
        while (s) {
            slabs++;
            total_objs += s->total;
            free_objs += s->total - s->inuse;
            s = s->next;
        }
        printf("SLAB size=%d slabs=%d total=%d free=%d\n", c->size, slabs, total_objs, free_objs);
    }
}

/*
 * slab_reclaim_one: Attempt to reclaim one completely free slab page.
 *
 * Linux mapping:
 * - Linux slab allocators can free empty slabs under memory pressure via
 *   shrinkers / cache reaping.
 * - Lite keeps the hook minimal: reclaim at most one fully free slab page
 *   from the built-in size-class caches.
 */
int slab_reclaim_one(void)
{
    for (int i = 0; i < SLAB_MAX_CACHE; i++) {
        struct kmem_cache *c = &caches[i];
        struct slab *prev = NULL;
        struct slab *s = c->slabs;
        while (s) {
            if (s->inuse == 0 && s->vaddr) {
                if (prev)
                    prev->next = s->next;
                else
                    c->slabs = s->next;

                for (uint32_t j = 0; j < slab_pages; j++) {
                    if (slab_map[j] == s) {
                        slab_map[j] = NULL;
                        break;
                    }
                }

                uint32_t phys = virt_to_phys_addr(s->vaddr);
                free_page((unsigned long)phys);
                s->vaddr = NULL;
                s->cache = NULL;
                slab_free_meta(s);
                return 1;
            }
            prev = s;
            s = s->next;
        }
    }
    return 0;
}

/* kmem_cache_init: Initialize kmem cache. */
void kmem_cache_init(void)
{
    for (int i = 0; i < SLAB_MAX_PAGES - 1; i++)
        slab_pool[i].next = &slab_pool[i + 1];
    slab_pool[SLAB_MAX_PAGES - 1].next = NULL;
    slab_free_list = &slab_pool[0];
    for (int i = 0; i < SLAB_MAX_CACHE; i++) {
        caches[i].size = cache_sizes[i];
        caches[i].slabs = NULL;
    }
    memset(slab_map, 0, sizeof(slab_map));
    slab_pages = 0;
    printf("SLAB: Initialized\n");
}
