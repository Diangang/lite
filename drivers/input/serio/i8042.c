#include "linux/interrupt.h"
#include "linux/serio.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/init.h"
#include "linux/platform_device.h"
#include "linux/slab.h"

struct i8042_port {
    struct serio *serio;
};

#define I8042_KBD_PORT_NO 0
#define I8042_NUM_PORTS 1

static struct i8042_port i8042_ports[I8042_NUM_PORTS];
static struct platform_device *i8042_platform_device;

static void i8042_port_release(struct device *dev)
{
    (void)dev;
}

static struct pt_regs *i8042_irq(struct pt_regs *regs)
{
    struct serio *serio = i8042_ports[I8042_KBD_PORT_NO].serio;

    (void)regs;
    if (!serio)
        return regs;
    uint8_t scancode = inb(0x60);
    serio_interrupt(serio, scancode);
    return regs;
}

static int i8042_hw_init(struct device *parent)
{
    struct serio *serio;

    if (i8042_ports[I8042_KBD_PORT_NO].serio)
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

    serio = (struct serio *)kmalloc(sizeof(*serio));
    if (!serio)
        return -1;
    memset(serio, 0, sizeof(*serio));
    serio->name = "i8042";
    serio->id.type = SERIO_8042;
    serio->id.proto = SERIO_ANY;
    serio->id.id = SERIO_ANY;
    serio->id.extra = SERIO_ANY;
    /* Provider responsibilities: set parent + release + id, then register. */
    serio->parent = parent;
    serio->dev.release = i8042_port_release;
    if (serio_register_port(serio) != 0) {
        kfree(serio);
        return -1;
    }
    i8042_ports[I8042_KBD_PORT_NO].serio = serio;

    printf("i8042 initialized.\n");
    return 0;
}

static void i8042_hw_exit(void)
{
    struct serio *serio = i8042_ports[I8042_KBD_PORT_NO].serio;

    if (!serio)
        return;
    serio_unregister_port(serio);
    i8042_ports[I8042_KBD_PORT_NO].serio = NULL;
    /* Best-effort: drop handler to avoid stale pointer use. */
    register_irq_handler(IRQ_KEYBOARD, 0);
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
    /*
     * Linux mapping: linux2.6 i8042 uses platform_create_bundle() which
     * registers both the platform_driver and its platform_device.
     * Keep board code minimal by instantiating the i8042 platform device here.
     */
    if (platform_driver_register(&i8042_driver) != 0)
        return -1;
    i8042_platform_device = platform_device_register_simple("i8042", 0);
    if (!i8042_platform_device) {
        platform_driver_unregister(&i8042_driver);
        return -1;
    }
    return 0;
}
module_init(i8042_init);
