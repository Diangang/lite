#include "linux/serio.h"
#include "linux/tty.h"
#include "linux/init.h"

/* US Keyboard Layout Scancode Table (Set 1) */
static const unsigned char kbdus[256] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b',
    '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,
    '*',
    0,
    ' ',
};

static void atkbd_interrupt(struct serio *serio, uint8_t data)
{
    (void)serio;
    /* Ignore key releases and unknown scancodes. */
    if (data & 0x80)
        return;
    unsigned char c = kbdus[data];
    if (c != 0)
        tty_receive_char((char)c);
}

static int atkbd_connect(struct serio *serio, struct serio_driver *drv)
{
    (void)drv;
    if (!serio)
        return -1;
    /* Minimal: accept i8042-like serio ports only. */
    return 0;
}

static const struct serio_device_id atkbd_serio_ids[] = {
    { .type = SERIO_8042, .proto = SERIO_ANY, .id = SERIO_ANY, .extra = SERIO_ANY },
    { 0 }
};

static struct serio_driver atkbd_drv = {
    .description = "AT keyboard",
    .id_table = atkbd_serio_ids,
    .connect = atkbd_connect,
    .disconnect = NULL,
    .interrupt = atkbd_interrupt,
    .driver = { .name = "atkbd" },
};

static int atkbd_init(void)
{
    return serio_register_driver(&atkbd_drv);
}
module_init(atkbd_init);
