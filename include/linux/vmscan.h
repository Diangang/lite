#ifndef LINUX_VMSCAN_H
#define LINUX_VMSCAN_H

#include "linux/mmzone.h"

void wakeup_kswapd(struct zone *zone);
void kswapd_init(void);
uint32_t kswapd_wakeup_count(void);
uint32_t kswapd_try_count(void);
uint32_t kswapd_reclaim_count(void);
uint32_t kswapd_anon_reclaim_count(void);
uint32_t kswapd_file_reclaim_count(void);
uint32_t try_to_free_pages(struct zone *zone, unsigned int order);

#endif
