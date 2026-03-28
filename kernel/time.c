#include "linux/time.h"

volatile uint32_t jiffies = 0;
static uint32_t time_hz = HZ;

void time_set_hz(uint32_t hz)
{
    if (hz)
        time_hz = hz;
}

uint32_t time_get_hz(void)
{
    return time_hz;
}

void time_tick(void)
{
    jiffies++;
}

uint32_t time_get_jiffies(void)
{
    return jiffies;
}

uint32_t time_get_uptime(void)
{
    if (!time_hz)
        return 0;
    return jiffies / time_hz;
}
