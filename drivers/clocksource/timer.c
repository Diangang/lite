#include "linux/timer.h"
#include "linux/interrupt.h"
#include "linux/libc.h"
#include "linux/time.h"

/*
 * The PIT (Programmable Interval Timer) has an internal clock running at 1193180 Hz.
 * We divide this by our desired frequency to get the divisor to send to the PIT.
 */
#define PIT_INTERNAL_FREQ 1193180

/* Timer Interrupt Handler (IRQ0) */
static struct pt_regs *timer_callback(struct pt_regs *regs)
{
    (void)regs;
    time_tick();
    return regs;
}

void init_timer(uint32_t frequency)
{
    if (!frequency)
        frequency = HZ;
    time_set_hz(frequency);

    /* Register our timer callback */
    register_interrupt_handler(IRQ0, timer_callback);

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
}

uint32_t timer_get_ticks(void)
{
    return time_get_jiffies();
}

uint32_t timer_get_uptime(void)
{
    return time_get_uptime();
}
