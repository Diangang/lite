#include "linux/clockevents.h"
#include "linux/time.h"

static struct clock_event_device *tick_device;

void clockevents_register_device(struct clock_event_device *dev)
{
    tick_device = dev;
}

int tick_set_periodic(uint32_t hz)
{
    if (!hz)
        hz = HZ;
    /*
     * Linux mapping: jiffies tick rate is HZ (compile-time constant). Lite
     * only supports periodic tick at HZ here; callers must not attempt to
     * change the tick base frequency dynamically.
     */
    if (hz != HZ)
        return -1;
    if (!tick_device || !tick_device->set_periodic)
        return -1;
    return tick_device->set_periodic(hz);
}

void tick_handle_periodic(void)
{
    time_tick();
}

