#ifndef LINUX_TTY_H
#define LINUX_TTY_H

#include <stdint.h>

#define TTY_FLAG_ECHO  0x1
#define TTY_FLAG_CANON 0x2

void tty_init(void);
void tty_set_user_exit_hook(void (*hook)(void));
void tty_set_foreground_pid(uint32_t pid);
uint32_t tty_get_foreground_pid(void);
uint32_t tty_get_flags(void);
void tty_set_flags(uint32_t flags);
void tty_receive_char(char c);
uint32_t tty_read_blocking(char *buf, uint32_t len);

#endif
