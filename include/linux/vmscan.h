#ifndef LINUX_VMSCAN_H
#define LINUX_VMSCAN_H

#include "linux/mmzone.h"

/*
 * Linux mapping: mm/vmscan.c uses struct scan_control to pass reclaim knobs
 * down the call chain (may_writepage/may_unmap/may_swap, etc).
 *
 * Lite keeps a minimal subset to express the semantic boundaries, without
 * implementing full LRU scanning heuristics.
 */
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
