#ifndef PROCFS_INTERNAL_H
#define PROCFS_INTERNAL_H

#include <stdint.h>
#include "linux/libc.h"

uint32_t task_dump_tasks(char *buf, uint32_t len);
uint32_t task_dump_maps(char *buf, uint32_t len);
uint32_t task_dump_maps_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_stat_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_cmdline_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_status_pid(uint32_t pid, char *buf, uint32_t len);
uint32_t task_dump_fd_pid(uint32_t pid, uint32_t fd, char *buf, uint32_t len);
uint32_t task_dump_cwd_pid(uint32_t pid, char *buf, uint32_t len);
const char *task_get_cwd(void);

static inline void proc_buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || cap == 0 || !s)
        return;
    while (*s && *off + 1 < cap) {
        buf[*off] = *s;
        (*off)++;
        s++;
    }
}

static inline void proc_buf_append_u32(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[16];
    itoa((int)v, 10, tmp);
    proc_buf_append(buf, off, cap, tmp);
}

static inline void proc_buf_append_hex(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[16];
    itoa((int)v, 16, tmp);
    proc_buf_append(buf, off, cap, "0x");
    proc_buf_append(buf, off, cap, tmp);
}

#endif
