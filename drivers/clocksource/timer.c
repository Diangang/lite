#include "linux/clockevents.h"
#include "linux/interrupt.h"
#include "linux/libc.h"
#include "linux/time.h"
#include "linux/timer.h"

/*
 * The PIT (Programmable Interval Timer) has an internal clock running at 1193180 Hz.
 * We divide this by our desired frequency to get the divisor to send to the PIT.
 */
#define PIT_INTERNAL_FREQ 1193180

/* Timer Interrupt Handler (IRQ0) */
static struct pt_regs *timer_callback(struct pt_regs *regs)
{
    (void)regs;
    tick_handle_periodic();
    return regs;
}

static int pit_set_periodic(uint32_t frequency)
{
    if (!frequency)
        frequency = HZ;

    /* The value we send to the PIT is the value to divide it's input clock
     * (1193180 Hz) by, to get our required frequency.
     * Divisor must fit into 16-bits.
     */
    uint32_t divisor = PIT_INTERNAL_FREQ / frequency;

    /* Send the command byte:
     * 0x36 = 00110110b
     * Channel 0, Lobyte/Hibyte, Rate Generator Mode, Binary
     */
    outb(0x43, 0x36);

    /* Send the frequency divisor, byte by byte */
    uint8_t l = (uint8_t)(divisor & 0xFF);
    uint8_t h = (uint8_t)((divisor >> 8) & 0xFF);

    /* Send the frequency divisor to channel 0 data port (0x40) */
    outb(0x40, l);
    outb(0x40, h);
    return 0;
}

static struct clock_event_device pit_clockevent = {
    .name = "pit",
    .set_periodic = pit_set_periodic,
    .shutdown = NULL,
};

/* init_timer: Initialize timer. */
void init_timer(uint32_t frequency)
{
    if (!frequency || frequency != HZ)
        frequency = HZ;

    /* Register our timer callback */
    register_irq_handler(IRQ_TIMER, timer_callback);

    clockevents_register_device(&pit_clockevent);
    (void)tick_set_periodic(frequency);
}
