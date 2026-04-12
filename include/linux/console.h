#ifndef LINUX_CONSOLE_H
#define LINUX_CONSOLE_H

#include <stdint.h>

struct device_type;

struct console {
    const char *name;
    void (*write)(const uint8_t *buf, uint32_t len);
    struct console *next;
};

int register_console(struct console *con);
void unregister_console(struct console *con);
void console_put_char(char c);
uint32_t console_write(const uint8_t *buf, uint32_t len);
extern const struct device_type console_device_type;

#endif
