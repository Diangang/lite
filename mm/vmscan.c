#include "linux/vmscan.h"
#include "linux/swap.h"

static int kswapd_running = 0;
static uint32_t kswapd_wakeups = 0;

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

uint32_t try_to_free_pages(struct zone *zone, unsigned int order)
{
    if (!zone)
        return 0;
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
            return 1u << order;
        }
    }
    return 0;
}
