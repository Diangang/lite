#ifndef LINUX_TIMER_H
#define LINUX_TIMER_H

#include <stdint.h>

void init_timer(uint32_t frequency);
uint32_t timer_get_ticks(void);
uint32_t timer_get_uptime(void);

#endif
