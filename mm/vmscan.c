#include "linux/vmscan.h"
#include "linux/swap.h"
#include "linux/pagemap.h"
#include "linux/slab.h"

static int kswapd_running = 0;
static uint32_t kswapd_wakeups = 0;
static uint32_t kswapd_tries = 0;
static uint32_t kswapd_reclaims = 0;
static uint32_t kswapd_anon_reclaims = 0;
static uint32_t kswapd_file_reclaims = 0;

/* wakeup_kswapd: Implement wakeup kswapd. */
void wakeup_kswapd(struct zone *zone)
{
    (void)zone;
    if (!kswapd_running)
        return;
    kswapd_wakeups++;
}

/* kswapd_init: Initialize kswapd. */
void kswapd_init(void)
{
    kswapd_running = 1;
}

/* kswapd_wakeup_count: Implement kswapd wakeup count. */
uint32_t kswapd_wakeup_count(void)
{
    return kswapd_wakeups;
}

/* kswapd_try_count: Implement kswapd try count. */
uint32_t kswapd_try_count(void)
{
    return kswapd_tries;
}

/* kswapd_reclaim_count: Implement kswapd reclaim count. */
uint32_t kswapd_reclaim_count(void)
{
    return kswapd_reclaims;
}

/* kswapd_anon_reclaim_count: Implement kswapd anon reclaim count. */
uint32_t kswapd_anon_reclaim_count(void)
{
    return kswapd_anon_reclaims;
}

/* kswapd_file_reclaim_count: Implement kswapd file reclaim count. */
uint32_t kswapd_file_reclaim_count(void)
{
    return kswapd_file_reclaims;
}

/* try_to_free_pages_sc: Implement try to free pages with reclaim knobs. */
uint32_t try_to_free_pages_sc(struct zone *zone, unsigned int order, struct scan_control *sc)
{
    if (!zone)
        return 0;
    if (!sc)
        return 0;
    kswapd_tries++;

    /*
     * File reclaim: allow dropping clean pagecache pages only when unmapping is
     * permitted. If writeback is permitted, try to flush dirty pages once and
     * then attempt to drop a now-clean pagecache page.
     */
    if (sc->may_unmap) {
        if (page_cache_reclaim_one()) {
            kswapd_reclaims++;
            kswapd_file_reclaims++;
            return 1u << order;
        }
        if (sc->may_writepage) {
            if (writeback_flush_all() > 0 && page_cache_reclaim_one()) {
                kswapd_reclaims++;
                kswapd_file_reclaims++;
                return 1u << order;
            }
        }
        if (slab_reclaim_one()) {
            kswapd_reclaims++;
            kswapd_file_reclaims++;
            return 1u << order;
        }
    }

    /* Anon reclaim: swap-out requires both unmapping and swapping permission. */
    if (sc->may_unmap && sc->may_swap && !list_empty(&zone->inactive_list)) {
        struct page *pg = list_first_entry(&zone->inactive_list, struct page, lru);

        /* Isolate one page from inactive list to avoid repeated rescans. */
        list_del(&pg->lru);
        if (zone->nr_inactive)
            zone->nr_inactive--;
        INIT_LIST_HEAD(&pg->lru);
        pg->flags &= ~PG_LRU;
        pg->flags |= PG_ISOLATED;

        if (swap_out_page(pg) == 0) {
            kswapd_reclaims++;
            kswapd_anon_reclaims++;
            return 1u << order;
        }

        /* Putback on failure. */
        pg->flags &= ~PG_ISOLATED;
        pg->flags |= PG_LRU;
        list_add_tail(&pg->lru, &zone->inactive_list);
        zone->nr_inactive++;
    }
    return 0;
}

/* try_to_free_pages: Default reclaim knobs (allow file reclaim + writeback + swap). */
uint32_t try_to_free_pages(struct zone *zone, unsigned int order)
{
    struct scan_control sc = {
        .may_writepage = 1,
        .may_unmap = 1,
        .may_swap = 1,
    };
    return try_to_free_pages_sc(zone, order, &sc);
}
