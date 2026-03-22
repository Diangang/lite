#include "syscall.h"
#include "task.h"
#include "libc.h"
#include "string.h"
#include "shell.h"
#include "vmm.h"
#include "fs.h"
#include "file.h"
#include "kheap.h"

static inline uint32_t irq_save(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint32_t flags)
{
    __asm__ volatile("push %0; popf" :: "r"(flags) : "memory", "cc");
}

#define SYSCALL_RETURN() do { irq_restore(irq_flags); return regs; } while (0)

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

struct linux_dirent {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char d_name[]; // Flexible array member
} __attribute__((packed));

void init_syscall(void)
{
    register_interrupt_handler(128, syscall_handler);
}

struct registers *syscall_handler(struct registers *regs)
{
    uint32_t irq_flags = irq_save();
    int from_user = (regs->cs & 0x3) == 0x3;
    if (regs->eax == SYS_WRITE) {
        if (regs->edx > 4096) {
            regs->eax = (uint32_t)-1;
            SYSCALL_RETURN();
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 0)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_READ) {
        if (regs->edx > 4096) {
            regs->eax = (uint32_t)-1;
            SYSCALL_RETURN();
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 1)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_OPEN) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_CLOSE) {
    } else if (regs->eax == SYS_BRK) {
    } else if (regs->eax == SYS_GETCWD) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, regs->ecx, 1)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_GETDENT) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 1)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_GETDENTS) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 1)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_CHDIR) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_MKDIR) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_EXECVE) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_CHMOD) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_WAITPID) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 1)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_IOCTL) {
        if (from_user && regs->edx != 0) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->edx, 4, 1)) {
                regs->eax = (uint32_t)-1;
                SYSCALL_RETURN();
            }
        }
    } else if (regs->eax == SYS_KILL) {
    } else if (regs->eax == SYS_MMAP) {
    } else if (regs->eax == SYS_MUNMAP) {
    } else if (regs->eax == SYS_FORK) {
    }

    switch (regs->eax) {
        case SYS_WRITE:
        {
            int fd = (int)regs->ebx;
            task_fd_t *d = task_fd_get(fd);
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

                if (from_user) {
                    if (vmm_copyin(tmp, (void*)(regs->ecx + off), chunk) != 0) {
                        regs->eax = (uint32_t)-1;
                        SYSCALL_RETURN();
                    }
                } else {
                    memcpy(tmp, (void*)(regs->ecx + off), chunk);
                }

                uint32_t n = file_write(d->file, (uint8_t*)tmp, chunk);
                if (n == 0) break;
                off += n;
                if (n < chunk) break;
            }
            regs->eax = off;
            break;
        }
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
                struct registers *task_schedule(struct registers *r);
                regs = task_schedule(regs);
            } else {
                task_exit();
            }
            regs->eax = 0;
            break;
        case SYS_READ:
        {
            int fd = (int)regs->ebx;
            task_fd_t *d = task_fd_get(fd);
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

                uint32_t n = file_read(d->file, (uint8_t*)tmp, chunk);
                if (n == 0) break;

                if (from_user) {
                    if (vmm_copyout((void*)(regs->ecx + off), tmp, n) != 0) {
                        regs->eax = (uint32_t)-1;
                        SYSCALL_RETURN();
                    }
                } else {
                    memcpy((void*)(regs->ecx + off), tmp, n);
                }

                off += n;
                if (n < chunk) break;
            }
            regs->eax = off;
            break;
        }
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
            uint32_t flags = regs->ecx;
            struct file *f = file_open_path(name, flags);
            if (!f) {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)task_fd_alloc(f);
            break;
        }
        case SYS_CLOSE: {
            regs->eax = (uint32_t)task_fd_close((int)regs->ebx);
            break;
        }
        case SYS_BRK: {
            uint32_t req = regs->ebx;
            if (from_user) {
                if (req != 0) {
                    if (req < 0x1000 || req >= 0xC0000000) {
                        regs->eax = task_brk(0);
                        break;
                    }
                }
            }
            regs->eax = task_brk(req);
            break;
        }
        case SYS_CHDIR: {
            char path[128];
            if (from_user) {
                if (copyin_cstr(path, sizeof(path), (const char*)regs->ebx) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                strcpy(path, (const char*)regs->ebx);
            }
            regs->eax = (uint32_t)(vfs_chdir(path) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_GETCWD: {
            uint32_t cap = regs->ecx;
            if (cap == 0) {
                regs->eax = 0;
                break;
            }
            const char *cwd = vfs_getcwd();
            if (!cwd) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t n = (uint32_t)strlen(cwd) + 1;
            if (n > cap) n = cap;
            if (from_user) {
                if (vmm_copyout((void*)regs->ebx, cwd, n) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                memcpy((void*)regs->ebx, cwd, n);
            }
            regs->eax = n;
            break;
        }
        case SYS_MKDIR: {
            char path[128];
            if (from_user) {
                if (copyin_cstr(path, sizeof(path), (const char*)regs->ebx) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                strcpy(path, (const char*)regs->ebx);
            }
            regs->eax = (uint32_t)(vfs_mkdir(path) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_GETDENT: {
            int fd = (int)regs->ebx;
            task_fd_t *d = task_fd_get(fd);
            if (!d || !d->file || !d->file->node || !d->file->vf) {
                regs->eax = (uint32_t)-1;
                break;
            }
            if ((d->file->node->flags & 0x7) != FS_DIRECTORY) {
                regs->eax = (uint32_t)-1;
                break;
            }
            if (!vfs_check_access(d->file->node, 1, 0, 1)) {
                printf("DEBUG: sys_getdents fd %d access denied\n", fd);
                regs->eax = (uint32_t)-1;
                break;
            }

            struct dirent *de = readdir_fs(d->file->node, d->file->vf->pos);
            if (!de) {
                regs->eax = 0;
                break;
            }
            d->file->vf->pos++;

            uint32_t out_cap = regs->edx;
            if (out_cap < sizeof(struct dirent)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            if (from_user) {
                if (vmm_copyout((void*)regs->ecx, de, sizeof(struct dirent)) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                memcpy((void*)regs->ecx, de, sizeof(struct dirent));
            }
            regs->eax = sizeof(struct dirent);
            break;
        }
        case SYS_GETDENTS: {
            int fd = (int)regs->ebx;
            task_fd_t *d = task_fd_get(fd);
            if (!d || !d->file || !d->file->node || !d->file->vf) {
                regs->eax = (uint32_t)-1;
                break;
            }
            if ((d->file->node->flags & 0x7) != FS_DIRECTORY) {
                regs->eax = (uint32_t)-1;
                break;
            }
            if (!vfs_check_access(d->file->node, 1, 0, 1)) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t out_cap = regs->edx;
            if (out_cap < 16) {
                regs->eax = (uint32_t)-1;
                break;
            }
            char *out = (char*)kmalloc(out_cap);
            if (!out) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t off = 0;
            while (off + 12 <= out_cap) {
                struct dirent *de = readdir_fs(d->file->node, d->file->vf->pos);
                if (!de) {
                    break;
                }
                uint32_t name_len = (uint32_t)strlen(de->name);
                uint32_t reclen = 10 + name_len + 1;
                reclen = (reclen + 3) & ~3;
                if (off + reclen > out_cap) {
                    if (off == 0) {
                        kfree(out);
                        regs->eax = (uint32_t)-1;
                        goto getdents_end;
                    }
                    break;
                }

                struct linux_dirent *lde = (struct linux_dirent*)(out + off);
                memset(lde, 0, reclen);
                lde->d_ino = de->ino;
                lde->d_off = d->file->vf->pos + 1;
                lde->d_reclen = (uint16_t)reclen;
                memcpy(lde->d_name, de->name, name_len);
                lde->d_name[name_len] = 0;

                off += reclen;
                d->file->vf->pos++;
            }
            if (off == 0) {
                kfree(out);
                regs->eax = 0;
                break;
            }
            if (from_user) {
                if (vmm_copyout((void*)regs->ecx, out, off) != 0) {
                    kfree(out);
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                memcpy((void*)regs->ecx, out, off);
            }
            kfree(out);
            regs->eax = off;
getdents_end:
            break;
        }
        case SYS_EXECVE: {
            char path[128];
            if (from_user) {
                if (copyin_cstr(path, sizeof(path), (const char*)regs->ebx) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                strcpy(path, (const char*)regs->ebx);
            }
            regs->eax = (uint32_t)(task_execve(path, regs) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_WAITPID: {
            uint32_t pid = regs->ebx;
            int code = 0;
            int reason = 0;
            uint32_t info0 = 0;
            uint32_t info1 = 0;
            if (task_wait(pid, &code, &reason, &info0, &info1) != 0) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t out_cap = regs->edx;
            if (out_cap < 16) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t tmp[4];
            tmp[0] = (uint32_t)code;
            tmp[1] = (uint32_t)reason;
            tmp[2] = info0;
            tmp[3] = info1;
            if (from_user) {
                if (vmm_copyout((void*)regs->ecx, tmp, 16) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                memcpy((void*)regs->ecx, tmp, 16);
            }
            regs->eax = 0;
            break;
        }
        case SYS_IOCTL: {
            int fd = (int)regs->ebx;
            uint32_t req = regs->ecx;
            uint32_t arg = regs->edx;
            task_fd_t *d = task_fd_get(fd);
            if (!d || !d->file || !d->file->node) {
                regs->eax = (uint32_t)-1;
                break;
            }
            regs->eax = (uint32_t)file_ioctl(d->file, req, arg);
            break;
        }
        case SYS_KILL: {
            uint32_t pid = regs->ebx;
            int sig = (int)regs->ecx;
            regs->eax = (uint32_t)(task_kill(pid, sig) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_MMAP: {
            uint32_t addr = regs->ebx;
            uint32_t len = regs->ecx;
            uint32_t prot = regs->edx;
            uint32_t res = task_mmap(addr, len, prot);
            regs->eax = res ? res : (uint32_t)-1;
            break;
        }
        case SYS_MUNMAP: {
            uint32_t addr = regs->ebx;
            uint32_t len = regs->ecx;
            regs->eax = (uint32_t)(task_munmap(addr, len) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_FORK: {
            regs->eax = (uint32_t)task_fork(regs);
            break;
        }
        case SYS_UMASK: {
            regs->eax = task_set_umask(regs->ebx);
            break;
        }
        case SYS_CHMOD: {
            char path[128];
            if (from_user) {
                if (copyin_cstr(path, sizeof(path), (const char*)regs->ebx) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                strcpy(path, (const char*)regs->ebx);
            }
            regs->eax = (uint32_t)(vfs_chmod(path, regs->ecx) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_GETUID:
            regs->eax = task_get_uid();
            break;
        case SYS_GETGID:
            regs->eax = task_get_gid();
            break;
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
    irq_restore(irq_flags);
    return regs;
}
