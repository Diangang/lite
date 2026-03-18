#include "syscall.h"
#include "kernel.h"
#include "task.h"
#include "shell.h"
#include "vmm.h"
#include "fs.h"

#define FD_MAX 16

typedef struct {
    int used;
    uint32_t owner_id;
    fs_node_t *node;
    uint32_t offset;
} file_desc_t;

static file_desc_t fd_table[FD_MAX];

static int copyin_cstr(char *dst, uint32_t dst_len, const char *src_user)
{
    if (!dst || dst_len == 0) return -1;
    if (!src_user) return -1;
    for (uint32_t i = 0; i + 1 < dst_len; i++) {
        char c = 0;
        if (vmm_copyin(&c, src_user + i, 1) != 0) return -1;
        dst[i] = c;
        if (c == 0) return 0;
    }
    dst[dst_len - 1] = 0;
    return 0;
}

static int fd_alloc(uint32_t owner_id, fs_node_t *node)
{
    if (!node) return -1;
    for (int i = 0; i < FD_MAX; i++) {
        if (!fd_table[i].used) {
            fd_table[i].used = 1;
            fd_table[i].owner_id = owner_id;
            fd_table[i].node = node;
            fd_table[i].offset = 0;
            return i + 3;
        }
    }
    return -1;
}

static file_desc_t *fd_get(uint32_t owner_id, int fd)
{
    if (fd < 3) return NULL;
    int idx = fd - 3;
    if (idx < 0 || idx >= FD_MAX) return NULL;
    if (!fd_table[idx].used) return NULL;
    if (fd_table[idx].owner_id != owner_id) return NULL;
    if (!fd_table[idx].node) return NULL;
    return &fd_table[idx];
}

static int fd_close(uint32_t owner_id, int fd)
{
    file_desc_t *d = fd_get(owner_id, fd);
    if (!d) return -1;
    d->used = 0;
    d->owner_id = 0;
    d->node = NULL;
    d->offset = 0;
    return 0;
}

void syscall_cleanup_task_fds(uint32_t owner_id)
{
    for (int i = 0; i < FD_MAX; i++) {
        if (fd_table[i].used && fd_table[i].owner_id == owner_id) {
            fd_table[i].used = 0;
            fd_table[i].owner_id = 0;
            fd_table[i].node = NULL;
            fd_table[i].offset = 0;
        }
    }
}

void syscall_init(void)
{
    register_interrupt_handler(128, syscall_handler);
}

static int syscall_write_user(const char *buf_user, uint32_t len)
{
    char tmp[128];
    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = len - off;
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
        if (vmm_copyin(tmp, buf_user + off, chunk) != 0) {
            return -1;
        }
        for (uint32_t i = 0; i < chunk; i++) {
            terminal_putchar(tmp[i]);
        }
        off += chunk;
    }
    return (int)len;
}

static int syscall_write_kernel(const char *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        terminal_putchar(buf[i]);
    }
    return (int)len;
}

static int syscall_read_user(char *buf_user, uint32_t len)
{
    if (!buf_user || len == 0) return 0;
    if (len > 256) len = 256;

    char tmp[256];
    uint32_t total = shell_read_blocking(tmp, len);
    if (vmm_copyout(buf_user, tmp, total) != 0) {
        return -1;
    }
    return (int)total;
}

static int syscall_read_kernel(char *buf, uint32_t len)
{
    if (!buf || len == 0) return 0;
    return (int)shell_read_blocking(buf, len);
}

void syscall_handler(registers_t *regs)
{
    int from_user = (regs->cs & 0x3) == 0x3;
    if (regs->eax == SYS_WRITE) {
        if (regs->ecx > 4096) {
            regs->eax = (uint32_t)-1;
            return;
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, regs->ecx, 0)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_READ) {
        if (regs->ecx > 4096) {
            regs->eax = (uint32_t)-1;
            return;
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, regs->ecx, 1)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_OPEN) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_FREAD) {
        if (regs->edx > 4096) {
            regs->eax = (uint32_t)-1;
            return;
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 1)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_CLOSE) {
    }

    switch (regs->eax) {
        case SYS_WRITE:
            if (from_user) {
                regs->eax = (uint32_t)syscall_write_user((const char *)regs->ebx, regs->ecx);
            } else {
                regs->eax = (uint32_t)syscall_write_kernel((const char *)regs->ebx, regs->ecx);
            }
            break;
        case SYS_YIELD:
            task_yield();
            regs->eax = 0;
            break;
        case SYS_SLEEP:
            task_sleep(regs->ebx);
            task_yield();
            regs->eax = 0;
            break;
        case SYS_EXIT:
            if (from_user) {
                task_exit_with_status((int)regs->ebx);
            } else {
                task_exit();
            }
            regs->eax = 0;
            break;
        case SYS_READ:
            if (from_user) {
                regs->eax = (uint32_t)syscall_read_user((char *)regs->ebx, regs->ecx);
            } else {
                regs->eax = (uint32_t)syscall_read_kernel((char *)regs->ebx, regs->ecx);
            }
            break;
        case SYS_GETPID:
            regs->eax = task_get_current_id();
            break;
        case SYS_OPEN: {
            char name[128];
            if (from_user) {
                if (copyin_cstr(name, sizeof(name), (const char*)regs->ebx) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                strcpy(name, (const char*)regs->ebx);
            }
            if (!fs_root) {
                regs->eax = (uint32_t)-1;
                break;
            }
            fs_node_t *node = finddir_fs(fs_root, name);
            if (!node) {
                regs->eax = (uint32_t)-1;
                break;
            }
            int fd = fd_alloc(task_get_current_id(), node);
            regs->eax = (uint32_t)fd;
            break;
        }
        case SYS_FREAD: {
            uint32_t owner = task_get_current_id();
            file_desc_t *d = fd_get(owner, (int)regs->ebx);
            if (!d) {
                regs->eax = (uint32_t)-1;
                break;
            }
            char tmp[256];
            uint32_t off = 0;
            uint32_t want = regs->edx;
            while (off < want) {
                uint32_t chunk = want - off;
                if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
                uint32_t n = read_fs(d->node, d->offset, chunk, (uint8_t*)tmp);
                if (n == 0) break;
                if (from_user) {
                    if (vmm_copyout((void*)(regs->ecx + off), tmp, n) != 0) {
                        regs->eax = (uint32_t)-1;
                        return;
                    }
                } else {
                    memcpy((void*)(regs->ecx + off), tmp, n);
                }
                d->offset += n;
                off += n;
                if (n < chunk) break;
            }
            regs->eax = off;
            break;
        }
        case SYS_CLOSE: {
            uint32_t owner = task_get_current_id();
            regs->eax = (uint32_t)fd_close(owner, (int)regs->ebx);
            break;
        }
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
