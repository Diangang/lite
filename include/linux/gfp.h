#ifndef LINUX_GFP_H
#define LINUX_GFP_H

#include <stdint.h>
#include "linux/mmzone.h"

typedef unsigned int gfp_t;

#define GFP_KERNEL 0
#define GFP_DMA 0x1
#define __GFP_ZERO 0x8000u

struct page *__alloc_pages_nodemask(gfp_t gfp_mask, unsigned int order,
                                    struct zonelist *zonelist, void *nodemask);
void __free_pages(struct page *page, unsigned int order);

void *alloc_pages(gfp_t gfp, unsigned int order);
static inline void *alloc_page(gfp_t gfp)
{
    return alloc_pages(gfp, 0);
}
unsigned long __get_free_pages(gfp_t gfp, unsigned int order);
static inline unsigned long __get_free_page(gfp_t gfp)
{
    return __get_free_pages(gfp, 0);
}

static inline unsigned long __get_dma_pages(gfp_t gfp, unsigned int order)
{
    return __get_free_pages(gfp | GFP_DMA, order);
}

unsigned long get_zeroed_page(gfp_t gfp);

void free_pages(unsigned long addr, unsigned int order);
static inline void free_page(unsigned long addr)
{
    free_pages(addr, 0);
}

void get_page(unsigned long addr);
unsigned int page_ref_count(unsigned long addr);

#endif
