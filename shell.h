#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

void shell_init(void);
void shell_process_char(char c);
uint32_t shell_read(char *buf, uint32_t len);

#endif
