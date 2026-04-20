#include "linux/interrupt.h"
#include "linux/serio.h"
#include "linux/libc.h"
#include "linux/init.h"
#include "linux/platform_device.h"

static struct serio i8042_port;
static int i8042_initialized;

static void i8042_port_release(struct device *dev)
{
    (void)dev;
}

static struct pt_regs *i8042_irq(struct pt_regs *regs)
{
    (void)regs;
    uint8_t scancode = inb(0x60);
    serio_interrupt(&i8042_port, scancode);
    return regs;
}

static int i8042_hw_init(struct device *parent)
{
    if (i8042_initialized)
        return 0;
    register_irq_handler(IRQ_KEYBOARD, i8042_irq);

    /* Clear output buffer. */
    while (inb(0x64) & 1)
        inb(0x60);

    /* Enable IRQ1 in PS/2 controller command byte. */
    while (inb(0x64) & 2)
        ;
    outb(0x64, 0x20);
    while ((inb(0x64) & 1) == 0)
        ;
    uint8_t status = (uint8_t)inb(0x60);
    status |= 1;
    status &= (uint8_t)~0x10;
    while (inb(0x64) & 2)
        ;
    outb(0x64, 0x60);
    while (inb(0x64) & 2)
        ;
    outb(0x60, status);

    /* Enable scanning. */
    while (inb(0x64) & 2)
        ;
    outb(0x60, 0xF4);

    i8042_port.name = "i8042";
    i8042_port.id.type = SERIO_8042;
    i8042_port.id.proto = SERIO_ANY;
    i8042_port.id.id = SERIO_ANY;
    i8042_port.id.extra = SERIO_ANY;
    /* Provider responsibilities: set parent + release + id, then register. */
    i8042_port.parent = parent;
    i8042_port.dev.release = i8042_port_release; /* static port */
    serio_register_port(&i8042_port);

    i8042_initialized = 1;
    printf("i8042 initialized.\n");
    return 0;
}

static void i8042_hw_exit(void)
{
    if (!i8042_initialized)
        return;
    serio_unregister_port(&i8042_port);
    /* Best-effort: drop handler to avoid stale pointer use. */
    register_irq_handler(IRQ_KEYBOARD, 0);
    i8042_initialized = 0;
}

static int i8042_platform_probe(struct platform_device *pdev)
{
    return i8042_hw_init(pdev ? &pdev->dev : NULL);
}

static void i8042_platform_remove(struct platform_device *pdev)
{
    (void)pdev;
    i8042_hw_exit();
}

static const struct platform_device_id i8042_platform_ids[] = {
    { .name = "i8042", .driver_data = 0 },
    { .name = NULL, .driver_data = 0 }
};

static struct platform_driver i8042_driver = {
    .driver = { .name = "i8042" },
    .id_table = i8042_platform_ids,
    .probe = i8042_platform_probe,
    .remove = i8042_platform_remove,
};

static int i8042_init(void)
{
    return platform_driver_register(&i8042_driver);
}
module_init(i8042_init);
