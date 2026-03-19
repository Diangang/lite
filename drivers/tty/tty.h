#ifndef TTY_H
#define TTY_H

#include <stdint.h>

enum {
    TTY_FLAG_ECHO = 1 << 0,
    TTY_FLAG_CANON = 1 << 1
};

void tty_init(void);
void tty_receive_char(char c);

uint32_t tty_read_blocking(char *buf, uint32_t len);
uint32_t tty_get_flags(void);
void tty_set_flags(uint32_t flags);

void tty_set_foreground_pid(uint32_t pid);
uint32_t tty_get_foreground_pid(void);

void tty_set_user_exit_hook(void (*hook)(void));

#endif

