#include "linux/time.h"

volatile uint32_t jiffies = 0;

/* time_set_hz: Implement time set hz. */
void time_set_hz(uint32_t hz)
{
    /*
     * Linux mapping: jiffies tick base is the compile-time HZ constant.
     * Lite documents the same fixed-rate rule and ignores dynamic changes.
     */
    (void)hz;
}

/* time_get_hz: Implement time get hz. */
uint32_t time_get_hz(void)
{
    return HZ;
}

/* time_tick: Implement time tick. */
void time_tick(void)
{
    jiffies++;
}

/* time_get_jiffies: Implement time get jiffies. */
uint32_t time_get_jiffies(void)
{
    return jiffies;
}

/* time_get_uptime: Implement time get uptime. */
uint32_t time_get_uptime(void)
{
    return jiffies / HZ;
}

