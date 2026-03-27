#include "linux/console.h"
#include "linux/libc.h"
#include "linux/tty.h"
#include "linux/init.h"
#include "linux/interrupt.h"

static int is_transmit_empty() {
   return inb(0x3f8 + 5) & 0x20;
}

void serial_put_char(char a) {
   while (is_transmit_empty() == 0);
   outb(0x3f8, a);
}

struct pt_regs *serial_callback(struct pt_regs *regs) {
    /* Check if it's a read interrupt (IIR) */
    /* Read the character */
    if (inb(0x3f8 + 5) & 1) {
        char c = inb(0x3f8);
        /* Pass to shell */
        tty_receive_char(c);
    }
    return regs;
}

/* Early Serial Console Initialization */
void init_serial() {
   outb(0x3f8 + 1, 0x00); // Disable all interrupts
   outb(0x3f8 + 3, 0x80); // Enable DLAB (set baud rate divisor)
   outb(0x3f8 + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
   outb(0x3f8 + 1, 0x00); // (hi byte)
   outb(0x3f8 + 3, 0x03); // 8 bits, no parity, one stop bit
   outb(0x3f8 + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold

   // DO NOT enable IRQs yet, this is just early console for polling output
   console_set_targets(CONSOLE_TARGET_SERIAL);
   tty_set_output_targets(TTY_OUTPUT_SERIAL);
}

/* Full Serial Driver Initialization */
static int serial_driver_init(void) {
   register_interrupt_handler(IRQ4, serial_callback);
   outb(0x3f8 + 4, 0x0B); // IRQs enabled, RTS/DSR set
   outb(0x3f8 + 1, 0x01); // Enable Received Data Available Interrupt
   printf("Serial driver interrupts enabled.\n");
   return 0;
}
module_init(serial_driver_init);
