#include "linux/page_alloc.h"
#include "linux/libc.h"
#include "linux/bootmem.h"
#include "linux/memlayout.h"
#include "linux/mmzone.h"
#include "linux/vmscan.h"

static struct multiboot_info* cached_mbi = NULL;
static uint32_t total_memory_kb = 0;
static uint32_t total_pages = 0;
static uint32_t managed_pages_total = 0;
static int32_t* buddy_next = NULL;
static unsigned int buddy_max_order = 0;
static int buddy_ready = 0;

static struct zonelist *default_zonelist_for_gfp(gfp_t gfp_mask)
{
    if (gfp_mask & GFP_DMA)
        return &dma_zonelist;
    return &contig_zonelist;
}

static void alloc_build_scan_control(gfp_t gfp_mask, struct scan_control *sc)
{
    (void)gfp_mask;
    if (!sc)
        return;
    /*
     * Linux mapping: allocator slow path reaches reclaim through a reclaim
     * control structure rather than calling an unparameterized reclaim body.
     * Lite keeps the knobs minimal but makes the boundary explicit.
     */
    sc->may_writepage = 1;
    sc->may_unmap = 1;
    sc->may_swap = 1;
}

static void alloc_reclaim_slowpath(struct zone *zone, gfp_t gfp_mask, unsigned int order)
{
    struct scan_control sc;
    if (!zone)
        return;
    alloc_build_scan_control(gfp_mask, &sc);
    wakeup_kswapd(zone);
    shrink_zone(zone, order, &sc);
}

/* zone_free_pages: Implement zone free pages. */
unsigned long zone_free_pages(struct zone *zone)
{
    unsigned long free_pages = 0;
    if (!zone)
        return 0;
    for (int order = 0; order < MAX_ORDER; order++)
        free_pages += zone->free_area[order].nr_free << order;
    return free_pages;
}

/* buddy_list_add: Implement buddy list add. */
static void buddy_list_add(struct zone *zone, unsigned int order, int32_t page)
{
    if (!zone)
        return;
    if (page == 0)
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

/* buddy_list_remove: Implement buddy list remove. */
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

/* buddy_alloc: Implement buddy alloc. */
static int32_t buddy_alloc(struct zone *zone, unsigned int order)
{
    for (unsigned int o = order; o <= buddy_max_order; o++) {
        if (!zone)
            return -1;
        int32_t page = zone->free_area[o].free_list;
        if (page < 0)
            continue;
        if (page == 0) {
            zone->free_area[o].free_list = buddy_next[page];
            if (zone->free_area[o].nr_free)
                zone->free_area[o].nr_free--;
            buddy_next[page] = -1;
            struct page *pg0 = pfn_to_page(page);
            if (pg0)
                pg0->flags &= ~PG_BUDDY;
            continue;
        }
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

/* buddy_free_block: Implement buddy free block. */
static void buddy_free_block(struct zone *zone, uint32_t page, unsigned int order)
{
    if (!zone)
        return;
    uint32_t zone_end = zone->start_pfn + zone->spanned_pages;
    while (order < buddy_max_order) {
        uint32_t buddy = page ^ (1u << order);
        if (buddy < zone->start_pfn || buddy >= zone_end)
            break;
        uint32_t new_base = buddy < page ? buddy : page;
        if (new_base + (1u << (order + 1)) > zone_end)
            break;
        struct page *buddy_page = pfn_to_page(buddy);
        if (!buddy_page || !(buddy_page->flags & PG_BUDDY) || buddy_page->order != order)
            break;
        buddy_list_remove(zone, order, buddy);
        if (buddy < page)
            page = buddy;
        order++;
    }
    while (order > 0 && page + (1u << order) > zone_end)
        order--;
    buddy_list_add(zone, order, page);
}

/* zone_watermark_ok: Implement zone watermark ok. */
static int zone_watermark_ok(struct zone *zone, unsigned int order, int mark)
{
    unsigned long free_pages = zone_free_pages(zone);
    unsigned long needed = 1u << order;
    if (free_pages < needed)
        return 0;
    return (free_pages - needed) > zone->watermark[mark];
}


/* __alloc_pages_nodemask: Allocate pages nodemask. */
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
        zonelist = default_zonelist_for_gfp(gfp_mask);
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
                alloc_reclaim_slowpath(zone, gfp_mask, order);
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

/* alloc_pages: Allocate pages. */
void *alloc_pages(gfp_t gfp, unsigned int order)
{
    for (int tries = 0; tries < 4; tries++) {
        struct page *page = __alloc_pages_nodemask(gfp, order, NULL, NULL);
        if (!page)
            return NULL;
        uint32_t pfn = page_to_pfn(page);
        if (pfn == 0) {
            page->flags |= PG_RESERVED;
            page->refcount = 1;
            continue;
        }
        return (void*)(pfn * PAGE_SIZE);
    }
    return NULL;
}

/* __get_free_pages: Get free pages. */
unsigned long __get_free_pages(gfp_t gfp, unsigned int order)
{
    void *p = alloc_pages(gfp, order);
    return (unsigned long)p;
}

/* get_zeroed_page: Get zeroed page. */
unsigned long get_zeroed_page(gfp_t gfp)
{
    unsigned long p = __get_free_pages(gfp, 0);
    if (p)
        memset(memlayout_directmap_phys_to_virt((uint32_t)p), 0, PAGE_SIZE);
    return p;
}

/* __free_pages: Free pages. */
void __free_pages(struct page *page, unsigned int order)
{
    if (!page)
        return;
    uint32_t frame = page_to_pfn(page);
    unsigned long addr = frame * PAGE_SIZE;
    free_pages(addr, order);
}

/* free_pages: Free pages. */
void free_pages(unsigned long addr, unsigned int order)
{
    if (addr >= PAGE_OFFSET)
        panic("PAGE_ALLOC PANIC: free_pages expects phys address (got vaddr)");
    if (addr & (PAGE_SIZE - 1))
        panic("PAGE_ALLOC PANIC: free_pages expects page-aligned phys address");
    uint32_t frame = addr / PAGE_SIZE;
    if (total_pages && frame >= total_pages)
        panic("PAGE_ALLOC PANIC: free_pages frame out of range");
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
        /*
         * Linux mapping: a page that returns to the buddy allocator must not
         * remain on reclaim lists. Keep this as a safety net; normal paths
         * should remove from LRU when unmapping.
         */
        if (pg->flags & PG_LRU)
            lru_del(pg);
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

/* get_page: Get page. */
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

/* page_ref_count: Implement page ref count. */
unsigned int page_ref_count(unsigned long addr)
{
    uint32_t frame = addr / PAGE_SIZE;

    struct page *pg = pfn_to_page(frame);
    if (!pg)
        return 0;
    return pg->refcount;
}

/* show_mem: Show mem. */
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
        struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)memlayout_directmap_phys_to_virt(mmap_phys);

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
            mmap = (struct multiboot_memory_map*)memlayout_directmap_phys_to_virt(mmap_phys);
        }
    } else {
        printf("BIOS did not provide a memory map.\n");
    }
}

/* buddy_max_order_get: Implement buddy max order get. */
unsigned int buddy_max_order_get(void)
{
    return buddy_max_order;
}

/* totalram_pages: Implement totalram pages. */
unsigned long totalram_pages(void)
{
    if (managed_pages_total)
        return managed_pages_total;
    return total_pages;
}

/* nr_free_pages: Return the number of currently free pages. */
unsigned long nr_free_pages(void)
{
    unsigned long free_pages = 0;
    if (contig_page_data.zone_dma.spanned_pages)
        free_pages += zone_free_pages(&contig_page_data.zone_dma);
    if (contig_page_data.zone_normal.spanned_pages)
        free_pages += zone_free_pages(&contig_page_data.zone_normal);
    return free_pages;
}

/* free_area_init: Free area init. */
void free_area_init(struct multiboot_info* mbi)
{
    free_area_init_core(mbi);
}

/* free_area_init_core: Free area init core. */
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

    buddy_next = (int32_t*)memlayout_directmap_phys_to_virt((uint32_t)meta);
    buddy_max_order = MAX_ORDER - 1;
    while (buddy_max_order > 0 && (1u << buddy_max_order) > total_pages)
        buddy_max_order--;

    for (uint32_t i = 0; i < total_pages; i++)
        buddy_next[i] = -1;
    uint32_t present = bootmem_present_pages(0, total_pages);
    uint32_t span_mb = (bootmem_lowmem_end() + (1024 * 1024) - 1) / (1024 * 1024);
    printf("PAGE_ALLOC: spanned %u pages, present %u pages (lowmem=%u MB)\n",
           total_pages, present, span_mb);
}

/* mem_init: Initialize mem. */
void mem_init(void)
{
    if (!total_pages || !buddy_next)
        return;
    if (!cached_mbi || !(cached_mbi->flags & (1 << 6)))
        panic("PMM PANIC: BIOS did not provide a memory map!");

    uint32_t mmap_phys = cached_mbi->mmap_addr;
    uint32_t mmap_end = mmap_phys + cached_mbi->mmap_length;
    struct multiboot_memory_map* mmap = (struct multiboot_memory_map*)memlayout_directmap_phys_to_virt(mmap_phys);
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
        mmap = (struct multiboot_memory_map*)memlayout_directmap_phys_to_virt(mmap_phys);
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
    setup_per_zone_wmarks();
    managed_pages_total = contig_page_data.zone_dma.managed_pages + contig_page_data.zone_normal.managed_pages;
    buddy_ready = 1;
}
