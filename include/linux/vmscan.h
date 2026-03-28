#ifndef LINUX_VMSCAN_H
#define LINUX_VMSCAN_H

#include "linux/mmzone.h"

void wakeup_kswapd(struct zone *zone);
void kswapd_init(void);
uint32_t kswapd_wakeup_count(void);

#endif
