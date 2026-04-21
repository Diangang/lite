#ifndef LINUX_CLOCKCHIPS_H
#define LINUX_CLOCKCHIPS_H

#include <stdint.h>

/*
 * Linux mapping: clockevent declarations live in include/linux/clockchips.h
 * while timekeeping consumes those events via a generic tick layer.
 *
 * Lite only needs the periodic subset for now.
 */
struct clock_event_device {
    const char *name;
    int (*set_periodic)(uint32_t hz);
    void (*shutdown)(void);
};

void clockevents_register_device(struct clock_event_device *dev);
int tick_set_periodic(uint32_t hz);
void tick_handle_periodic(void);

#endif

