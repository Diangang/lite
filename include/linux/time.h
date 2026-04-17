#ifndef LINUX_TIME_H
#define LINUX_TIME_H

#include <stdint.h>

#define HZ 100

extern volatile uint32_t jiffies;

void time_set_hz(uint32_t hz);
uint32_t time_get_hz(void);
void time_tick(void);
uint32_t time_get_jiffies(void);
uint32_t time_get_uptime(void);

/*
 * Linux mapping: generic timeout/conversion helpers are expressed in jiffies.
 * Lite keeps a fixed HZ tick and uses wrap-safe comparisons for deadlines.
 */
static inline uint32_t msecs_to_jiffies(uint32_t msecs)
{
    /*
     * Lite rule: HZ is fixed at 100, so 1 jiffy == 10ms.
     * Avoid 64-bit division helpers (e.g. __udivdi3) in freestanding builds.
     */
    if (msecs == 0)
        return 0;
    return (msecs + 9u) / 10u;
}

static inline uint32_t jiffies_to_msecs(uint32_t ticks)
{
    return ticks * 10u;
}

static inline int time_after_eq(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) >= 0;
}

static inline int time_before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

#endif
