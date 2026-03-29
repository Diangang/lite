#include "linux/syscall.h"
#include "linux/syscalls.h"
#include "linux/sched.h"
#include "linux/fork.h"
#include "linux/exit.h"
#include "linux/signal.h"
#include "linux/binfmts.h"
#include "linux/cred.h"
#include "linux/mm.h"
#include "linux/irqflags.h"
#include "linux/interrupt.h"
#include "linux/libc.h"
#include "asm/page.h"
#include "linux/uaccess.h"

static struct pt_regs *syscall_handler(struct pt_regs *regs)
{
    uint32_t irq_flags = irq_save();
    int from_user = (regs->cs & 0x3) == 0x3;

    switch (regs->eax) {
        case SYS_WRITE:
            regs->eax = (uint32_t)sys_write((int)regs->ebx, (const void*)regs->ecx, regs->edx, from_user);
            break;
        case SYS_YIELD:
            task_yield();
            regs->eax = 0;
            break;
        case SYS_SLEEP:
            task_sleep(regs->ebx);
            regs->eax = 0;
            break;
        case SYS_EXIT:
            if (from_user) {
                sys_exit((int)regs->ebx);
                regs = task_schedule(regs);
            } else {
                do_exit(0);
            }
            regs->eax = 0;
            break;
        case SYS_READ:
            regs->eax = (uint32_t)sys_read((int)regs->ebx, (void*)regs->ecx, regs->edx, from_user);
            break;
        case SYS_GETPID:
            regs->eax = task_get_current_id();
            break;
        case SYS_OPEN:
            regs->eax = (uint32_t)sys_open((const char*)regs->ebx, regs->ecx, from_user);
            break;
        case SYS_CLOSE:
            regs->eax = (uint32_t)sys_close((int)regs->ebx);
            break;
        case SYS_BRK: {
            uint32_t req = regs->ebx;
            if (from_user) {
                if (req != 0) {
                    if (req < 0x1000 || req >= TASK_SIZE) {
                        regs->eax = sys_brk(0);
                        break;
                    }
                }
            }
            regs->eax = sys_brk(req);
            break;
        }
        case SYS_CHDIR:
            regs->eax = (uint32_t)sys_chdir((const char*)regs->ebx, from_user);
            break;
        case SYS_GETCWD:
            regs->eax = (uint32_t)sys_getcwd((char*)regs->ebx, regs->ecx, from_user);
            break;
        case SYS_UNLINK:
            regs->eax = (uint32_t)sys_unlink((const char*)regs->ebx, from_user);
            break;
        case SYS_MKDIR:
            regs->eax = (uint32_t)sys_mkdir((const char*)regs->ebx, from_user);
            break;
        case SYS_RMDIR:
            regs->eax = (uint32_t)sys_rmdir((const char*)regs->ebx, from_user);
            break;
        case SYS_GETDENTS:
            regs->eax = (uint32_t)sys_getdents((int)regs->ebx, (void*)regs->ecx, regs->edx, from_user);
            break;
        case SYS_EXECVE: {
            char path[128];
            if (from_user) {
                if (strncpy_from_user(path, sizeof(path), (const char*)regs->ebx) != 0) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
            } else {
                strcpy(path, (const char*)regs->ebx);
            }
            regs->eax = (uint32_t)(sys_execve(path, regs) == 0 ? 0 : (uint32_t)-1);
            break;
        }
        case SYS_WAITPID:
            regs->eax = (uint32_t)sys_waitpid(regs->ebx, (void*)regs->ecx, regs->edx, from_user);
            break;
        case SYS_IOCTL:
            regs->eax = (uint32_t)sys_ioctl((int)regs->ebx, regs->ecx, regs->edx);
            break;
        case SYS_KILL:
            regs->eax = (uint32_t)(sys_kill(regs->ebx, (int)regs->ecx) == 0 ? 0 : (uint32_t)-1);
            break;
        case SYS_MMAP: {
            uint32_t res = sys_mmap(regs->ebx, regs->ecx, regs->edx);
            regs->eax = res ? res : (uint32_t)-1;
            break;
        }
        case SYS_MUNMAP:
            regs->eax = (uint32_t)(sys_munmap(regs->ebx, regs->ecx) == 0 ? 0 : (uint32_t)-1);
            break;
        case SYS_MPROTECT:
            regs->eax = (uint32_t)(sys_mprotect(regs->ebx, regs->ecx, regs->edx) ? 0 : (uint32_t)-1);
            break;
        case SYS_MREMAP: {
            uint32_t res = sys_mremap(regs->ebx, regs->ecx, regs->edx);
            regs->eax = res ? res : (uint32_t)-1;
            break;
        }
        case SYS_FORK:
            regs->eax = (uint32_t)sys_fork(regs);
            break;
        case SYS_UMASK:
            regs->eax = task_set_umask(regs->ebx);
            break;
        case SYS_CHMOD:
            regs->eax = (uint32_t)sys_chmod((const char*)regs->ebx, regs->ecx, from_user);
            break;
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

    if (task_should_resched())
        regs = task_schedule(regs);

    return regs;
}

void init_syscall(void)
{
    register_interrupt_handler(128, syscall_handler);
}
