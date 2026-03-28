#ifndef LINUX_MMZONE_H
#define LINUX_MMZONE_H

#include <stdint.h>

#define MAX_ORDER 11
#define PG_BUDDY 0x1
#define PG_RESERVED 0x2
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

struct page {
    uint32_t flags;
    uint16_t refcount;
    uint16_t order;
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
void refresh_zone_watermarks(void);
struct page *pfn_to_page(uint32_t pfn);
uint32_t page_to_pfn(struct page *page);
struct zone *pfn_to_zone(uint32_t pfn);

#endif
