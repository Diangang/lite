#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

enum console_target {
    CONSOLE_TARGET_VGA = 1 << 0,
    CONSOLE_TARGET_SERIAL = 1 << 1,
    CONSOLE_TARGET_ALL = CONSOLE_TARGET_VGA | CONSOLE_TARGET_SERIAL
};

void console_set_targets(uint32_t targets);
uint32_t console_get_targets(void);
void console_put_char(char c);
void console_put_str(const char *s);
uint32_t console_write(const uint8_t *buf, uint32_t len);

#endif
