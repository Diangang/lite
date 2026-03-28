#ifndef LINUX_PARAMS_H
#define LINUX_PARAMS_H

extern char saved_command_line[256];

void setup_command_line(const char *cmdline);
const char *get_init_process(void);

#endif
