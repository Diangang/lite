#include "linux/vmscan.h"
#include "linux/swap.h"
#include "linux/pagemap.h"

static int kswapd_running = 0;
static uint32_t kswapd_wakeups = 0;
static uint32_t kswapd_tries = 0;
static uint32_t kswapd_reclaims = 0;
static uint32_t kswapd_anon_reclaims = 0;
static uint32_t kswapd_file_reclaims = 0;

void wakeup_kswapd(struct zone *zone)
{
    (void)zone;
    if (!kswapd_running)
        return;
    kswapd_wakeups++;
}

void kswapd_init(void)
{
    kswapd_running = 1;
}

uint32_t kswapd_wakeup_count(void)
{
    return kswapd_wakeups;
}

uint32_t kswapd_try_count(void)
{
    return kswapd_tries;
}

uint32_t kswapd_reclaim_count(void)
{
    return kswapd_reclaims;
}

uint32_t kswapd_anon_reclaim_count(void)
{
    return kswapd_anon_reclaims;
}

uint32_t kswapd_file_reclaim_count(void)
{
    return kswapd_file_reclaims;
}

uint32_t try_to_free_pages(struct zone *zone, unsigned int order)
{
    if (!zone)
        return 0;
    kswapd_tries++;
    if (page_cache_reclaim_one()) {
        kswapd_reclaims++;
        kswapd_file_reclaims++;
        return 1u << order;
    }
    uint32_t start = zone->start_pfn;
    uint32_t end = zone->start_pfn + zone->spanned_pages;
    for (uint32_t pfn = start; pfn < end; pfn++) {
        struct page *pg = pfn_to_page(pfn);
        if (!pg)
            continue;
        if (pg->flags & (PG_BUDDY | PG_RESERVED))
            continue;
        if (pg->mapcount == 0)
            continue;
        if (swap_out_page(pg) == 0) {
            kswapd_reclaims++;
            if (pg->map_mm)
                kswapd_anon_reclaims++;
            else
                kswapd_file_reclaims++;
            return 1u << order;
        }
    }
    return 0;
}
