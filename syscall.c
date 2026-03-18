#include "syscall.h"
#include "kernel.h"
#include "task.h"
#include "shell.h"
#include "vmm.h"
#include "fs.h"
#include "file.h"

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

void syscall_init(void)
{
    register_interrupt_handler(128, syscall_handler);
}

void syscall_handler(registers_t *regs)
{
    int from_user = (regs->cs & 0x3) == 0x3;
    if (regs->eax == SYS_WRITE) {
        if (regs->edx > 4096) {
            regs->eax = (uint32_t)-1;
            return;
        }
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ecx, regs->edx, 0)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_READ) {
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
    } else if (regs->eax == SYS_OPEN) {
        if (from_user) {
            if (!vmm_user_accessible(vmm_get_current_directory(), (void*)regs->ebx, 1, 0)) {
                regs->eax = (uint32_t)-1;
                return;
            }
        }
    } else if (regs->eax == SYS_CLOSE) {
    } else if (regs->eax == SYS_BRK) {
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
                        return;
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
                        return;
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
            if (!fs_root) {
                regs->eax = (uint32_t)-1;
                break;
            }
            uint32_t flags = regs->ecx;
            file_t *f = file_open_path(name, flags);
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
        default:
            regs->eax = (uint32_t)-1;
            break;
    }
}
