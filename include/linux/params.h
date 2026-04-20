#ifndef LINUX_PARAMS_H
#define LINUX_PARAMS_H

#include <stddef.h>

extern char saved_command_line[256];

void setup_command_line(const char *cmdline);
const char *get_execute_command(void);
int get_cmdline_param(const char *key, char *value, size_t cap);

#endif
