#include "linux/vmscan.h"

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
