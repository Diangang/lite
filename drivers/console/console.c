#include "console.h"
#include "serial.h"
#include "vga.h"

static uint32_t console_targets = 0;

void console_set_targets(uint32_t targets)
{
    console_targets |= targets;
}

uint32_t console_get_targets(void)
{
    return console_targets;
}

void console_put_char(char c)
{
    if (console_targets & CONSOLE_TARGET_VGA) vga_put_char(c);
    if (console_targets & CONSOLE_TARGET_SERIAL) serial_put_char(c);
}

uint32_t console_write(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    for (uint32_t i = 0; i < len; i++) console_put_char((char)buf[i]);
    return len;
}
