#include "isr.h"
#include "tty.h"
#include "libc.h"
#include "init.h"

/* US Keyboard Layout Scancode Table (Set 1) */
unsigned char kbdus[256] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',     /* 9 */
  '9', '0', '-', '=', '\b',     /* Backspace */
  '\t',                 /* Tab */
  'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',     /* Enter key */
    0,                  /* 29   - Control */
  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,            /* Left shift */
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0,                  /* Right shift */
  '*',
    0,  /* Alt */
  ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* 69 - Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
  '-',
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
  '+',
    0,  /* 79 - End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0,  /* All other keys are undefined */
};

/* Keyboard Interrupt Handler */
struct pt_regs *keyboard_callback(struct pt_regs *regs)
{
    (void)regs;

    /* Read scancode from keyboard controller data port 0x60 */
    uint8_t scancode = inb(0x60);

    /* If the top bit of the byte is set, it means a key has been released */
    if (scancode & 0x80)
    {
        /* Key released - ignore for now */
    }
    else
    {
        /* Key pressed - pass the character to the shell */
        if (kbdus[scancode] != 0)
            tty_receive_char(kbdus[scancode]);
    }
    return regs;
}

static int keyboard_driver_init(void)
{
    /* Register IRQ1 (INT 33) handler */
    register_interrupt_handler(IRQ1, keyboard_callback);

    /* 1. Clear input buffer */
    while (inb(0x64) & 1)
        inb(0x60);

    /* 2. Enable Keyboard Interrupts in PS/2 Controller Command Byte */
    /* Wait for input buffer to be empty */
    while (inb(0x64) & 2);
    /* Send command: Read Command Byte */
    outb(0x64, 0x20);

    /* Wait for output buffer to be full */
    while ((inb(0x64) & 1) == 0);
    uint8_t status = inb(0x60);

    /* Set bit 0 (Enable IRQ1) and clear bit 4 (Disable Keyboard) if set */
    status |= 1;
    status &= ~0x10;

    /* Write Command Byte back */
    while (inb(0x64) & 2);
    outb(0x64, 0x60);
    while (inb(0x64) & 2);
    outb(0x60, status);

    /* 3. Reset Keyboard and Enable Scanning */
    /* Wait for input buffer to be empty */
    while (inb(0x64) & 2);
    outb(0x60, 0xF4); /* Enable scanning */

    printf("Keyboard driver initialized.\n");
    return 0;
}
module_init(keyboard_driver_init);
