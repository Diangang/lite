#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

void shell_init(void);
void shell_task(void);
void shell_process_char(char c);
uint32_t shell_read(char *buf, uint32_t len);
uint32_t shell_read_blocking(char *buf, uint32_t len);
uint32_t shell_tty_get_flags(void);
void shell_tty_set_flags(uint32_t flags);
uint32_t shell_tty_read_blocking(char *buf, uint32_t len);
void shell_set_foreground_pid(uint32_t pid);
uint32_t shell_get_foreground_pid(void);
void shell_set_foreground(int is_user);
void shell_on_user_exit(void);

#endif
