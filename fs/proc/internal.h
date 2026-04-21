#ifndef PROCFS_INTERNAL_H
#define PROCFS_INTERNAL_H

#include <stdint.h>
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"

struct inode;
struct dirent;

/* Lite-only procfs root registry helper (NO_DIRECT_LINUX_MATCH). */
void proc_register_root_child(const char *name, struct inode *inode);

/* Per-file init hooks used by fs/proc/root.c to populate fixed entries. */
void proc_meminfo_init(void);
void proc_interrupts_init(void);
void proc_generic_init(void);
void proc_pid_init(void);
struct inode *proc_get_pid_dir(uint32_t pid);
struct inode *proc_pid_lookup(const char *name);
struct dirent *proc_pid_readdir_root(uint32_t index);

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
