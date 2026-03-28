#include "linux/mmzone.h"
#include "linux/bootmem.h"
#include "asm/page.h"
#include "linux/libc.h"

struct pglist_data contig_page_data;
struct zonelist contig_zonelist;
struct zonelist dma_zonelist;
static struct page *mem_map = NULL;
static uint32_t mem_map_pages = 0;

void init_zones(void)
{
    uint32_t total = bootmem_total_pages();
    if (total == 0)
        return;
    mem_map_pages = total;
    mem_map = (struct page*)bootmem_alloc(sizeof(struct page) * total, PAGE_SIZE);
    if (!mem_map)
        return;
    memset(mem_map, 0, sizeof(struct page) * total);
    for (uint32_t i = 0; i < total; i++)
        mem_map[i].flags = PG_RESERVED;

    contig_page_data.node_id = 0;
    contig_page_data.node_start_pfn = 0;
    contig_page_data.node_spanned_pages = total;
    contig_page_data.node_present_pages = total;

    uint32_t dma_pages = total;
    if (dma_pages > MAX_DMA_PFN)
        dma_pages = MAX_DMA_PFN;

    contig_page_data.zone_dma.type = ZONE_DMA;
    contig_page_data.zone_dma.start_pfn = 0;
    contig_page_data.zone_dma.spanned_pages = dma_pages;
    contig_page_data.zone_dma.present_pages = dma_pages;
    contig_page_data.zone_dma.mem_map = mem_map;
    contig_page_data.zone_dma.watermark[WMARK_MIN] = dma_pages / 64;
    contig_page_data.zone_dma.watermark[WMARK_LOW] = dma_pages / 32;
    contig_page_data.zone_dma.watermark[WMARK_HIGH] = dma_pages / 16;
    for (int i = 0; i < MAX_ORDER; i++) {
        contig_page_data.zone_dma.free_area[i].free_list = -1;
        contig_page_data.zone_dma.free_area[i].nr_free = 0;
    }

    contig_page_data.zone_normal.type = ZONE_NORMAL;
    contig_page_data.zone_normal.start_pfn = dma_pages;
    contig_page_data.zone_normal.spanned_pages = total - dma_pages;
    contig_page_data.zone_normal.present_pages = total - dma_pages;
    contig_page_data.zone_normal.mem_map = mem_map;
    contig_page_data.zone_normal.watermark[WMARK_MIN] = (total - dma_pages) / 64;
    contig_page_data.zone_normal.watermark[WMARK_LOW] = (total - dma_pages) / 32;
    contig_page_data.zone_normal.watermark[WMARK_HIGH] = (total - dma_pages) / 16;
    for (int i = 0; i < MAX_ORDER; i++) {
        contig_page_data.zone_normal.free_area[i].free_list = -1;
        contig_page_data.zone_normal.free_area[i].nr_free = 0;
    }
}

void build_all_zonelists(void)
{
    contig_zonelist.nr_zones = 0;
    if (contig_page_data.zone_dma.spanned_pages)
        contig_zonelist.zones[contig_zonelist.nr_zones++] = &contig_page_data.zone_dma;
    if (contig_page_data.zone_normal.spanned_pages)
        contig_zonelist.zones[contig_zonelist.nr_zones++] = &contig_page_data.zone_normal;
    contig_zonelist.highest_zone = contig_zonelist.nr_zones ? contig_zonelist.zones[contig_zonelist.nr_zones - 1]->type : ZONE_DMA;
    contig_page_data.node_zonelists = &contig_zonelist;

    dma_zonelist.nr_zones = 0;
    if (contig_page_data.zone_dma.spanned_pages)
        dma_zonelist.zones[dma_zonelist.nr_zones++] = &contig_page_data.zone_dma;
    dma_zonelist.highest_zone = dma_zonelist.nr_zones ? dma_zonelist.zones[dma_zonelist.nr_zones - 1]->type : ZONE_DMA;
}

struct page *pfn_to_page(uint32_t pfn)
{
    if (!mem_map || pfn >= mem_map_pages)
        return NULL;
    return &mem_map[pfn];
}

uint32_t page_to_pfn(struct page *page)
{
    if (!mem_map || !page)
        return 0;
    return (uint32_t)(page - mem_map);
}

struct zone *pfn_to_zone(uint32_t pfn)
{
    if (pfn < contig_page_data.zone_dma.spanned_pages)
        return &contig_page_data.zone_dma;
    return &contig_page_data.zone_normal;
}
