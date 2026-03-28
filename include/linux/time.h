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

#endif
