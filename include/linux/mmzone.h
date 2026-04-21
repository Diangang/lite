#ifndef LINUX_MMZONE_H
#define LINUX_MMZONE_H

#include <stdint.h>
#include "linux/list.h"

#define MAX_ORDER 11
#define PG_BUDDY 0x1
#define PG_RESERVED 0x2
#define PG_LRU 0x4
#define PG_ISOLATED 0x8
#define NR_WMARK 3
#define MAX_DMA_PFN (16 * 1024 * 1024 / 4096)

enum zone_watermarks {
    WMARK_MIN = 0,
    WMARK_LOW = 1,
    WMARK_HIGH = 2
};

enum zone_type {
    ZONE_DMA = 0,
    ZONE_NORMAL = 1
};

struct multiboot_info;
struct mm_struct;
struct rmap_item;

struct page {
    uint32_t flags;
    uint16_t refcount;
    uint16_t order;
    uint16_t mapcount;
    struct mm_struct *map_mm;
    uint32_t map_vaddr;
    struct rmap_item *rmap_list;
    struct list_head lru;
};

struct free_area {
    int32_t free_list;
    uint32_t nr_free;
};

struct zone {
    enum zone_type type;
    uint32_t start_pfn;
    uint32_t spanned_pages;
    uint32_t present_pages;
    uint32_t managed_pages;
    struct page *mem_map;
    uint32_t watermark[NR_WMARK];
    struct free_area free_area[MAX_ORDER];
    struct list_head inactive_list;
    uint32_t nr_inactive;
};

struct pglist_data {
    uint32_t node_id;
    uint32_t node_start_pfn;
    uint32_t node_spanned_pages;
    uint32_t node_present_pages;
    struct zone zone_dma;
    struct zone zone_normal;
    struct zonelist *node_zonelists;
};

struct zonelist {
    struct zone *zones[2];
    uint32_t nr_zones;
    enum zone_type highest_zone;
};

extern struct pglist_data contig_page_data;
extern struct zonelist contig_zonelist;
extern struct zonelist dma_zonelist;

void init_zones(void);
void build_all_zonelists(void);
void setup_per_zone_wmarks(void);
struct page *pfn_to_page(uint32_t pfn);
uint32_t page_to_pfn(struct page *page);
struct zone *pfn_to_zone(uint32_t pfn);

/* Minimal LRU primitives (Linux mapping: inactive LRU + isolation). */
void lru_add_inactive(struct page *pg);
void lru_del(struct page *pg);


void free_area_init(struct multiboot_info *mbi);
void free_area_init_core(struct multiboot_info *mbi);
void mem_init(void);
unsigned long totalram_pages(void);
unsigned long nr_free_pages(void);
unsigned long zone_free_pages(struct zone *zone);
unsigned int buddy_max_order_get(void);
void show_mem(void);

/* Linux mapping: reclaim knobs are passed via scan_control. */
struct scan_control {
    int may_writepage;
    int may_unmap;
    int may_swap;
};

void wakeup_kswapd(struct zone *zone);
void kswapd_init(void);
uint32_t kswapd_wakeup_count(void);
uint32_t kswapd_try_count(void);
uint32_t kswapd_reclaim_count(void);
uint32_t kswapd_anon_reclaim_count(void);
uint32_t kswapd_file_reclaim_count(void);
uint32_t try_to_free_pages(struct zone *zone, unsigned int order);
uint32_t shrink_zone(struct zone *zone, unsigned int order, struct scan_control *sc);

#endif
