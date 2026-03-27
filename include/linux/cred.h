#ifndef LINUX_CRED_H
#define LINUX_CRED_H

#include <stdint.h>

uint32_t task_get_uid(void);
uint32_t task_get_gid(void);
uint32_t task_get_umask(void);
uint32_t task_set_umask(uint32_t mask);

#endif
