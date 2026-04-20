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

/*
 * Linux mapping: syscall dispatch uses a syscall table (sys_call_table) rather
 * than a monolithic switch. Lite keeps the existing syscall numbers (SYS_*)
 * but aligns the dispatch shape.
 */
typedef struct pt_regs *(*syscall_dispatch_t)(struct pt_regs *regs, int from_user);

static struct pt_regs *sys_ni_syscall(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)-1;
    return regs;
}

static struct pt_regs *sys_write_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_write((int)regs->ebx, (const void *)regs->ecx, regs->edx, from_user);
    return regs;
}

static struct pt_regs *sys_yield_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    task_yield();
    regs->eax = 0;
    return regs;
}

static struct pt_regs *sys_sleep_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    task_sleep(regs->ebx);
    regs->eax = 0;
    return regs;
}

static struct pt_regs *sys_exit_dispatch(struct pt_regs *regs, int from_user)
{
    if (from_user) {
        sys_exit((int)regs->ebx);
        regs = task_schedule(regs);
    } else {
        do_exit(0);
    }
    regs->eax = 0;
    return regs;
}

static struct pt_regs *sys_read_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_read((int)regs->ebx, (void *)regs->ecx, regs->edx, from_user);
    return regs;
}

static struct pt_regs *sys_getpid_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = task_get_current_id();
    return regs;
}

static struct pt_regs *sys_open_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_open((const char *)regs->ebx, regs->ecx, from_user);
    return regs;
}

static struct pt_regs *sys_close_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)sys_close((int)regs->ebx);
    return regs;
}

static struct pt_regs *sys_brk_dispatch(struct pt_regs *regs, int from_user)
{
    uint32_t req = regs->ebx;
    if (from_user) {
        if (req != 0) {
            if (req < 0x1000 || req >= TASK_SIZE) {
                regs->eax = sys_brk(0);
                return regs;
            }
        }
    }
    regs->eax = sys_brk(req);
    return regs;
}

static struct pt_regs *sys_chdir_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_chdir((const char *)regs->ebx, from_user);
    return regs;
}

static struct pt_regs *sys_getcwd_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_getcwd((char *)regs->ebx, regs->ecx, from_user);
    return regs;
}

static struct pt_regs *sys_unlink_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_unlink((const char *)regs->ebx, from_user);
    return regs;
}

static struct pt_regs *sys_mkdir_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_mkdir((const char *)regs->ebx, from_user);
    return regs;
}

static struct pt_regs *sys_rmdir_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_rmdir((const char *)regs->ebx, from_user);
    return regs;
}

static struct pt_regs *sys_getdents_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_getdents((int)regs->ebx, (void *)regs->ecx, regs->edx, from_user);
    return regs;
}

static struct pt_regs *sys_execve_dispatch(struct pt_regs *regs, int from_user)
{
    char path[128];
    if (from_user) {
        if (strncpy_from_user(path, sizeof(path), (const char *)regs->ebx) != 0) {
            regs->eax = (uint32_t)-1;
            return regs;
        }
    } else {
        strcpy(path, (const char *)regs->ebx);
    }
    regs->eax = (uint32_t)(sys_execve(path, regs) == 0 ? 0 : (uint32_t)-1);
    return regs;
}

static struct pt_regs *sys_waitpid_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_waitpid(regs->ebx, (void *)regs->ecx, regs->edx, from_user);
    return regs;
}

static struct pt_regs *sys_ioctl_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)sys_ioctl((int)regs->ebx, regs->ecx, regs->edx);
    return regs;
}

static struct pt_regs *sys_kill_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)(sys_kill(regs->ebx, (int)regs->ecx) == 0 ? 0 : (uint32_t)-1);
    return regs;
}

static struct pt_regs *sys_mmap_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    uint32_t res = sys_mmap(regs->ebx, regs->ecx, regs->edx);
    regs->eax = res ? res : (uint32_t)-1;
    return regs;
}

static struct pt_regs *sys_munmap_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)(sys_munmap(regs->ebx, regs->ecx) == 0 ? 0 : (uint32_t)-1);
    return regs;
}

static struct pt_regs *sys_mprotect_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)(sys_mprotect(regs->ebx, regs->ecx, regs->edx) ? 0 : (uint32_t)-1);
    return regs;
}

static struct pt_regs *sys_mremap_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    uint32_t res = sys_mremap(regs->ebx, regs->ecx, regs->edx);
    regs->eax = res ? res : (uint32_t)-1;
    return regs;
}

static struct pt_regs *sys_fork_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = (uint32_t)sys_fork(regs);
    return regs;
}

static struct pt_regs *sys_umask_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = sys_umask(regs->ebx);
    return regs;
}

static struct pt_regs *sys_chmod_dispatch(struct pt_regs *regs, int from_user)
{
    regs->eax = (uint32_t)sys_chmod((const char *)regs->ebx, regs->ecx, from_user);
    return regs;
}

static struct pt_regs *sys_getuid_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = current_uid();
    return regs;
}

static struct pt_regs *sys_getgid_dispatch(struct pt_regs *regs, int from_user)
{
    (void)from_user;
    regs->eax = current_gid();
    return regs;
}

static const syscall_dispatch_t sys_call_table[NR_syscalls] = {
    [SYS_WRITE] = sys_write_dispatch,
    [SYS_YIELD] = sys_yield_dispatch,
    [SYS_SLEEP] = sys_sleep_dispatch,
    [SYS_EXIT] = sys_exit_dispatch,
    [SYS_READ] = sys_read_dispatch,
    [SYS_GETPID] = sys_getpid_dispatch,
    [SYS_OPEN] = sys_open_dispatch,
    [SYS_CLOSE] = sys_close_dispatch,
    [SYS_BRK] = sys_brk_dispatch,
    [SYS_CHDIR] = sys_chdir_dispatch,
    [SYS_GETCWD] = sys_getcwd_dispatch,
    [SYS_UNLINK] = sys_unlink_dispatch,
    [SYS_MKDIR] = sys_mkdir_dispatch,
    [SYS_EXECVE] = sys_execve_dispatch,
    [SYS_WAITPID] = sys_waitpid_dispatch,
    [SYS_IOCTL] = sys_ioctl_dispatch,
    [SYS_KILL] = sys_kill_dispatch,
    [SYS_MMAP] = sys_mmap_dispatch,
    [SYS_MUNMAP] = sys_munmap_dispatch,
    [SYS_MPROTECT] = sys_mprotect_dispatch,
    [SYS_MREMAP] = sys_mremap_dispatch,
    [SYS_FORK] = sys_fork_dispatch,
    [SYS_GETDENTS] = sys_getdents_dispatch,
    [SYS_UMASK] = sys_umask_dispatch,
    [SYS_CHMOD] = sys_chmod_dispatch,
    [SYS_GETUID] = sys_getuid_dispatch,
    [SYS_GETGID] = sys_getgid_dispatch,
    [SYS_RMDIR] = sys_rmdir_dispatch,
};

/* syscall_handler: Handle syscall. */
static struct pt_regs *syscall_handler(struct pt_regs *regs)
{
    uint32_t irq_flags = irq_save();
    int from_user = (regs->cs & 0x3) == 0x3;

    uint32_t nr = regs->eax;
    if (nr < NR_syscalls && sys_call_table[nr])
        regs = sys_call_table[nr](regs, from_user);
    else
        regs = sys_ni_syscall(regs, from_user);
    irq_restore(irq_flags);

    if (task_should_resched())
        regs = task_schedule(regs);

    return regs;
}

/* syscall_init: Initialize syscall entry. */
void syscall_init(void)
{
    register_interrupt_handler(128, syscall_handler);
}
