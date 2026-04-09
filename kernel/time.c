#include "linux/time.h"

volatile uint32_t jiffies = 0;
static uint32_t time_hz = HZ;

/* time_set_hz: Implement time set hz. */
void time_set_hz(uint32_t hz)
{
    if (hz)
        time_hz = hz;
}

/* time_get_hz: Implement time get hz. */
uint32_t time_get_hz(void)
{
    return time_hz;
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
    if (!time_hz)
        return 0;
    return jiffies / time_hz;
}
