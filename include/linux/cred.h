#ifndef LINUX_CRED_H
#define LINUX_CRED_H

#include <stdint.h>

uint32_t current_uid(void);
uint32_t current_gid(void);
uint32_t current_umask(void);
uint32_t sys_umask(uint32_t mask);

#endif
