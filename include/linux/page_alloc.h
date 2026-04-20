#ifndef LINUX_PAGE_ALLOC_H
#define LINUX_PAGE_ALLOC_H

#include <stdint.h>
#include "asm/multiboot.h"
#include "asm/page.h"
#include "linux/mmzone.h"

typedef unsigned int gfp_t;

#define GFP_KERNEL 0
#define GFP_DMA 0x1

void free_area_init(struct multiboot_info* mbi);
void free_area_init_core(struct multiboot_info* mbi);
void mem_init(void);

struct page *__alloc_pages_nodemask(gfp_t gfp_mask, unsigned int order,
                                    struct zonelist *zonelist, void *nodemask);
void __free_pages(struct page *page, unsigned int order);

void *alloc_pages(gfp_t gfp, unsigned int order);
static inline void *alloc_page(gfp_t gfp)
{
    return alloc_pages(gfp, 0);
}
unsigned long __get_free_pages(gfp_t gfp, unsigned int order);
unsigned long get_zeroed_page(gfp_t gfp);

void free_pages(unsigned long addr, unsigned int order);
static inline void free_page(unsigned long addr)
{
    free_pages(addr, 0);
}

void get_page(unsigned long addr);
unsigned int page_ref_count(unsigned long addr);

unsigned long totalram_pages(void);
unsigned long nr_free_pages(void);
unsigned long zone_free_pages(struct zone *zone);

unsigned int buddy_max_order_get(void);

void show_mem(void);

#endif
