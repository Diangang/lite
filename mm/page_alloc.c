#include "linux/page_alloc.h"
#include "linux/libc.h"
#include "linux/bootmem.h"
#include "linux/mmzone.h"
#include "linux/vmscan.h"

static struct multiboot_info* cached_mbi = NULL;
static uint32_t total_memory_kb = 0;
static uint32_t total_pages = 0;
static int32_t* buddy_next = NULL;
static unsigned int buddy_max_order = 0;
static int buddy_ready = 0;

unsigned long zone_free_pages(struct zone *zone)
{
    unsigned long free_pages = 0;
    if (!zone)
        return 0;
    for (int order = 0; order < MAX_ORDER; order++)
        free_pages += zone->free_area[order].nr_free << order;
    return free_pages;
}

static void buddy_list_add(struct zone *zone, unsigned int order, int32_t page)
{
    if (!zone)
        return;
    buddy_next[page] = zone->free_area[order].free_list;
    zone->free_area[order].free_list = page;
    zone->free_area[order].nr_free++;
    struct page *pg = pfn_to_page(page);
    if (pg) {
        pg->flags |= PG_BUDDY;
        pg->order = order;
    }
}

static void buddy_list_remove(struct zone *zone, unsigned int order, int32_t page)
{
    if (!zone)
        return;
    int32_t prev = -1;
    int32_t cur = zone->free_area[order].free_list;
    while (cur >= 0) {
        if (cur == page) {
            if (prev < 0)
                zone->free_area[order].free_list = buddy_next[cur];
            else
                buddy_next[prev] = buddy_next[cur];
            buddy_next[cur] = -1;
            if (zone->free_area[order].nr_free)
                zone->free_area[order].nr_free--;
            struct page *pg = pfn_to_page(cur);
            if (pg)
                pg->flags &= ~PG_BUDDY;
            return;
        }
        prev = cur;
        cur = buddy_next[cur];
    }
}

static int32_t buddy_alloc(struct zone *zone, unsigned int order)
{
    for (unsigned int o = order; o <= buddy_max_order; o++) {
        if (!zone)
            return -1;
        int32_t page = zone->free_area[o].free_list;
        if (page < 0)
            continue;
        zone->free_area[o].free_list = buddy_next[page];
        if (zone->free_area[o].nr_free)
            zone->free_area[o].nr_free--;
        buddy_next[page] = -1;
        struct page *pg = pfn_to_page(page);
        if (pg)
            pg->flags &= ~PG_BUDDY;
        while (o > order) {
            o--;
            int32_t buddy = page + (1u << o);
            buddy_list_add(zone, o, buddy);
        }
        if (pg) {
            pg->flags &= ~PG_BUDDY;
            pg->order = order;
        }
        return page;
    }
    return -1;
}

static void buddy_free_block(struct zone *zone, uint32_t page, unsigned int order)
{
    if (!zone)
        return;
    while (order < buddy_max_order) {
        uint32_t buddy = page ^ (1u << order);
        if (buddy < zone->start_pfn || buddy >= zone->start_pfn + zone->spanned_pages)
            break;
        struct page *buddy_page = pfn_to_page(buddy);
        if (!buddy_page || !(buddy_page->flags & PG_BUDDY) || buddy_page->order != order)
            break;
        buddy_list_remove(zone, order, buddy);
        if (buddy < page)
            page = buddy;
        order++;
    }
    buddy_list_add(zone, order, page);
}

static int zone_watermark_ok(struct zone *zone, unsigned int order, int mark)
{
    unsigned long free_pages = zone_free_pages(zone);
    unsigned long needed = 1u << order;
    if (free_pages < needed)
        return 0;
    return (free_pages - needed) > zone->watermark[mark];
}


struct page *__alloc_pages_nodemask(gfp_t gfp_mask, unsigned int order,
                                    struct zonelist *zonelist, void *nodemask)
{
    (void)gfp_mask;
    (void)nodemask;
    if (!buddy_ready) {
        uint32_t bytes = PAGE_SIZE << order;
        void *addr = bootmem_alloc(bytes, PAGE_SIZE);
        if (!addr)
            return NULL;
        uint32_t frame = (uint32_t)addr / PAGE_SIZE;
        uint32_t run = 1u << order;
        for (uint32_t i = 0; i < run; i++) {
            struct page *pg = pfn_to_page(frame + i);
            if (pg)
                pg->refcount = 1;
        }
        return pfn_to_page(frame);
    }

    if (!zonelist) {
        if (gfp_mask & GFP_DMA)
            zonelist = &dma_zonelist;
        else
            zonelist = &contig_zonelist;
    }

    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t i = 0; i < zonelist->nr_zones; i++) {
            struct zone *zone = zonelist->zones[i];
            if (!zone)
                continue;
            if (pass == 0 && !zone_watermark_ok(zone, order, WMARK_HIGH))
                continue;
            if (pass == 1 && !zone_watermark_ok(zone, order, WMARK_LOW)) {
                wakeup_kswapd(zone);
                continue;
            }
            if (pass == 2 && !zone_watermark_ok(zone, order, WMARK_MIN)) {
                wakeup_kswapd(zone);
                try_to_free_pages(zone, order);
                continue;
            }
            int frame = buddy_alloc(zone, order);
            if (frame < 0)
                continue;
            uint32_t run = 1u << order;
            for (uint32_t j = 0; j < run; j++) {
                struct page *pg = pfn_to_page(frame + j);
                if (pg)
                    pg->refcount = 1;
            }
            return pfn_to_page(frame);
        }
    }

    return printf("PAGE_ALLOC: Out of memory!\n"), NULL;
}

void *alloc_pages(gfp_t gfp, unsigned int order)
{
    struct page *page = __alloc_pages_nodemask(gfp, order, &contig_zonelist, NULL);
    if (!page)
        return NULL;
    uint32_t pfn = page_to_pfn(page);
    return (void*)(pfn * PAGE_SIZE);
}

unsigned long __get_free_pages(gfp_t gfp, unsigned int order)
{
    void *p = alloc_pages(gfp, order);
    return (unsigned long)p;
}

unsigned long get_zeroed_page(gfp_t gfp)
{
    unsigned long p = __get_free_pages(gfp, 0);
    if (p)
        memset(phys_to_virt(p), 0, PAGE_SIZE);
    return p;
}

void __free_pages(struct page *page, unsigned int order)
{
    if (!page)
        return;
    uint32_t frame = page_to_pfn(page);
    unsigned long addr = frame * PAGE_SIZE;
    free_pages(addr, order);
}

void free_pages(unsigned long addr, unsigned int order)
{
    uint32_t frame = addr / PAGE_SIZE;
    uint32_t run = 1u << order;
    if (run == 0)
        return;
    if (!buddy_ready)
        return;

    int can_free = 1;
    for (uint32_t i = 0; i < run; i++) {
        uint32_t idx = frame + i;
        struct page *pg = pfn_to_page(idx);
        if (!pg)
            continue;
        if (pg->refcount > 1) {
            pg->refcount--;
            can_free = 0;
            continue;
        }
        if (pg->refcount == 1)
            pg->refcount = 0;
    }
    if (!can_free)
        return;
    struct zone *zone = pfn_to_zone(frame);
    buddy_free_block(zone, frame, order);
}

void get_page(unsigned long addr)
{
    uint32_t frame = addr / PAGE_SIZE;

    struct page *pg = pfn_to_page(frame);
    if (!pg)
        return;
    if (pg->refcount == 0)
        pg->refcount = 1;
    else if (pg->refcount < 0xFFFF)
        pg->refcount++;
}

unsigned int page_ref_count(unsigned long addr)
{
    uint32_t frame = addr / PAGE_SIZE;

    struct page *pg = pfn_to_page(frame);
    if (!pg)
        return 0;
    return pg->refcount;
}

void show_mem(void)
{
    if (!cached_mbi)
        return (void)printf("PAGE_ALLOC not initialized or Multiboot info not found.\n");

    if (cached_mbi->flags & 1)
        printf("Total Memory: %d KB (%d MB)\n", total_memory_kb, total_memory_kb / 1024);

    if (cached_mbi->flags & (1 << 6)) {
        printf("Memory Map provided by BIOS:\n");
        uint32_t mmap_phys = cached_mbi->mmap_addr;
        uint32_t mmap_end = mmap_phys + cached_mbi->mmap_length;
        struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)phys_to_virt(mmap_phys);

        while (mmap_phys < mmap_end) {
            const char* type_str = "Unknown";
            switch (mmap->type) {
            case MULTIBOOT_MEMORY_AVAILABLE:
                type_str = "Available RAM";
                break;
            case MULTIBOOT_MEMORY_RESERVED:
                type_str = "Reserved (Hardware)";
                break;
            case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                type_str = "ACPI Reclaimable";
                break;
            case MULTIBOOT_MEMORY_NVS:
                type_str = "ACPI NVS";
                break;
            case MULTIBOOT_MEMORY_BADRAM:
                type_str = "Bad RAM";
                break;
            }

            uint32_t size_kb = mmap->len_low / 1024;

            printf("  [0x%x - 0x%x] %d KB (%s)\n",
                   mmap->addr_low,
                   mmap->addr_low + mmap->len_low - 1,
                   size_kb, type_str);

            mmap_phys += mmap->size + sizeof(mmap->size);
            mmap = (struct multiboot_memory_map*)phys_to_virt(mmap_phys);
        }
    } else {
        printf("BIOS did not provide a memory map.\n");
    }
}

unsigned long totalram_pages(void)
{
    return total_pages;
}

unsigned long freeram_pages(void)
{
    unsigned long free_pages = 0;
    if (contig_page_data.zone_dma.spanned_pages)
        free_pages += zone_free_pages(&contig_page_data.zone_dma);
    if (contig_page_data.zone_normal.spanned_pages)
        free_pages += zone_free_pages(&contig_page_data.zone_normal);
    return free_pages;
}

void free_area_init(struct multiboot_info* mbi)
{
    free_area_init_core(mbi);
}

void free_area_init_core(struct multiboot_info* mbi)
{
    cached_mbi = mbi;

    if (mbi->flags & 1)
        total_memory_kb = mbi->mem_lower + mbi->mem_upper;

    total_pages = bootmem_total_pages();
    if (!total_pages)
        total_pages = (total_memory_kb * 1024) / PAGE_SIZE;

    uint32_t next_bytes = total_pages * sizeof(int32_t);
    uint32_t total_bytes = next_bytes;

    void *meta = bootmem_alloc(total_bytes, PAGE_SIZE);
    if (!meta)
        panic("PAGE_ALLOC PANIC: bootmem_alloc failed");

    buddy_next = (int32_t*)phys_to_virt((uint32_t)meta);
    buddy_max_order = MAX_ORDER - 1;
    while (buddy_max_order > 0 && (1u << buddy_max_order) > total_pages)
        buddy_max_order--;

    for (uint32_t i = 0; i < total_pages; i++)
        buddy_next[i] = -1;
    printf("PAGE_ALLOC: managing %d pages (%d MB)\n", total_pages, total_memory_kb / 1024);
}

void mem_init(void)
{
    if (!total_pages || !buddy_next)
        return;
    if (!cached_mbi || !(cached_mbi->flags & (1 << 6)))
        panic("PMM PANIC: BIOS did not provide a memory map!");

    uint32_t mmap_phys = cached_mbi->mmap_addr;
    uint32_t mmap_end = mmap_phys + cached_mbi->mmap_length;
    struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)phys_to_virt(mmap_phys);
    while (mmap_phys < mmap_end) {
        if (mmap->type != MULTIBOOT_MEMORY_AVAILABLE)
            goto next;

        if (mmap->addr_low < 0x100000)
            goto next;

        uint32_t start_addr = mmap->addr_low;
        uint32_t length = mmap->len_low;
        uint32_t start_page = start_addr / PAGE_SIZE;
        uint32_t num_pages = length / PAGE_SIZE;

        for (uint32_t i = 0; i < num_pages; i++) {
            uint32_t page_addr = (start_page + i) * PAGE_SIZE;
            if (start_page + i >= total_pages)
                break;
            if (bootmem_is_reserved(page_addr, PAGE_SIZE))
                continue;
            struct page *pg = pfn_to_page(start_page + i);
            if (pg)
                pg->flags &= ~PG_RESERVED;
        }

next:
        mmap_phys += mmap->size + sizeof(mmap->size);
        mmap = (struct multiboot_memory_map*)phys_to_virt(mmap_phys);
    }
    contig_page_data.zone_dma.managed_pages = 0;
    contig_page_data.zone_normal.managed_pages = 0;
    for (uint32_t i = 0; i < total_pages; i++) {
        struct page *pg = pfn_to_page(i);
        if (!pg)
            continue;
        if (pg->flags & PG_RESERVED)
            continue;
        struct zone *zone = pfn_to_zone(i);
        if (zone)
            zone->managed_pages++;
        if (pg->flags & PG_BUDDY)
            continue;
        buddy_free_block(zone, i, 0);
    }
    refresh_zone_watermarks();
    buddy_ready = 1;
}
