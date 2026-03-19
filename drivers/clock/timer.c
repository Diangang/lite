#include "timer.h"
#include "isr.h"
#include "kernel.h" /* For outb */

/*
 * The PIT (Programmable Interval Timer) has an internal clock running at 1193180 Hz.
 * We divide this by our desired frequency to get the divisor to send to the PIT.
 */
#define PIT_INTERNAL_FREQ 1193180

static uint32_t tick = 0;
static uint32_t current_frequency = 0;

/* Timer Interrupt Handler (IRQ0) */
static void timer_callback(registers_t *regs)
{
    (void)regs;
    tick++;
}

void init_timer(uint32_t frequency)
{
    current_frequency = frequency;

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
    return tick;
}

uint32_t timer_get_uptime(void)
{
    if (current_frequency == 0) return 0;
    return tick / current_frequency;
}