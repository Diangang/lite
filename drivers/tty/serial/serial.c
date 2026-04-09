#include "linux/console.h"
#include "linux/device.h"
#include "linux/libc.h"
#include "linux/tty.h"
#include "linux/init.h"
#include "linux/interrupt.h"

void serial_put_char(char a);

static void serial_console_write(const uint8_t *buf, uint32_t len)
{
    uint8_t saved_ier = (uint8_t)inb(0x3f8 + 1);
    outb(0x3f8 + 1, saved_ier & 0x01);
    for (uint32_t i = 0; i < len; i++)
        serial_put_char((char)buf[i]);
    outb(0x3f8 + 1, saved_ier);
}

static struct console serial_console = {
    .name = "ttyS0",
    .write = serial_console_write,
    .next = (struct console *)0,
};

static struct device_driver drv_serial;
static struct tty_driver tty_serial_driver;
static const struct device_id serial_ids[] = {
    { .name = "serial0", .type = "serial" },
    { .name = NULL, .type = NULL }
};

/* is_transmit_empty: Implement is transmit empty. */
static int is_transmit_empty() {
   return inb(0x3f8 + 5) & 0x20;
}

/* serial_put_char: Implement serial put char. */
void serial_put_char(char a) {
   while (is_transmit_empty() == 0);
   outb(0x3f8, a);
}

/* serial_callback: Implement serial callback. */
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
   outb(0x3f8 + 1, 0x00);
   outb(0x3f8 + 3, 0x80);
   outb(0x3f8 + 0, 0x03);
   outb(0x3f8 + 1, 0x00);
   outb(0x3f8 + 3, 0x03);
   outb(0x3f8 + 2, 0xC7);
   register_console(&serial_console);
   tty_set_output_targets(TTY_OUTPUT_SERIAL);
}

static int serial_probe(struct device *dev)
{
   (void)dev;
   register_interrupt_handler(IRQ4, serial_callback);
   outb(0x3f8 + 4, 0x0B);
   outb(0x3f8 + 1, 0x01);
   printf("Serial driver interrupts enabled.\n");
   return 0;
}

/* Full Serial Driver Initialization */
static int serial_driver_init(void) {
   struct bus_type *platform = device_model_platform_bus();
   if (!platform)
      return -1;
   if (tty_register_driver(&tty_serial_driver, "serial", 1) != 0)
      return -1;
   init_driver_ids(&drv_serial, "serial", platform, serial_ids, serial_probe);
   if (driver_register(&drv_serial) != 0)
      return -1;
   struct device *parent = device_model_find_device("serial0");
   if (!parent)
      return -1;
   if (!tty_register_device(&tty_serial_driver, 0, parent, "ttyS0", NULL))
      return -1;
   return 0;
}
module_init(serial_driver_init);
