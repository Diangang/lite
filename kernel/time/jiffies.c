#include "linux/time.h"

/*
 * Linux mapping: linux2.6/kernel/time/jiffies.c
 *
 * Lite keeps only the global tick counter here; conversion helpers and
 * uptime accessors remain in `kernel/time/time.c`.
 */
volatile uint32_t jiffies = 0;
