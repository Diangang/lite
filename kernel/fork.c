#include "linux/sched.h"
#include "linux/fork.h"
#include "linux/binfmts.h"
#include "linux/pid.h"
#include "linux/slab.h"
#include "linux/libc.h"
#include "linux/printk.h"
#include "linux/fs.h"
#include "linux/console.h"
#include "linux/irqflags.h"
#include "linux/panic.h"

/* copy_thread: Copy thread. */
struct pt_regs *copy_thread(uint32_t *stack, void (*entry)(void), struct pt_regs *parent_regs)
{
    if (!stack)
        return NULL;
    uint32_t stack_top = (uint32_t)stack + THREAD_SIZE;
    struct pt_regs *child_regs = (struct pt_regs*)(stack_top - sizeof(struct pt_regs));
    if (parent_regs) {
        memcpy(child_regs, parent_regs, sizeof(*parent_regs));
        child_regs->esp = stack_top;
        child_regs->ebp = 0;
        child_regs->eax = 0;
        return child_regs;
    }
    memset(child_regs, 0, sizeof(*child_regs));
    child_regs->ds = 0x10;
    child_regs->esp = stack_top;
    child_regs->ebp = 0;
    child_regs->int_no = 0;
    child_regs->err_code = 0;
    child_regs->eip = (uint32_t)entry;
    child_regs->cs = 0x08;
    child_regs->eflags = 0x202;
    child_regs->useresp = stack_top;
    child_regs->ss = 0x10;
    return child_regs;
}

/* set_task_comm: Set task comm. */
void set_task_comm(struct task_struct *task, const char *program)
{
    if (!task)
        return;
    task->comm[0] = 0;
    if (!program)
        return;
    uint32_t i = 0;
    while (program[i] && i + 1 < sizeof(task->comm)) {
        task->comm[i] = program[i];
        i++;
    }
    task->comm[i] = 0;
}

void __put_task_struct(struct task_struct *task)
{
    if (!task)
        return;
    if (!task_release_invariant_holds(task))
        panic("__put_task_struct invariant violated");
    if (atomic_read(&task->usage) != 0)
        panic("__put_task_struct with non-zero usage");
    if (task->thread.sp0)
        kfree((void *)((uint32_t)task->thread.sp0 - THREAD_SIZE));
    kfree(task);
}

/* sys_fork: Implement sys fork. */
int sys_fork(struct pt_regs *regs)
{
    struct task_struct *parent = task_current();
    if (!parent || !parent->mm || !regs)
        return -1;
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);
    if (!task || !stack) {
        if (task) kfree(task);
        if (stack) kfree(stack);
        return -1;
    }

    struct mm_struct *child_mm = dup_mm(parent->mm);
    if (!child_mm) {
        kfree(stack);
        kfree(task);
        return -1;
    }

    atomic_set(&task->usage, 1);
    task->pid = ++last_pid;
    task->parent = parent;
    task->thread.regs = copy_thread(stack, NULL, regs);
    if (!task->thread.regs) {
        mm_destroy(child_mm);
        kfree(stack);
        kfree(task);
        return -1;
    }
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    task->wake_jiffies = 0;
    task->state = TASK_RUNNABLE;
    task->time_slice = TASK_TIMESLICE_TICKS;
    task->mm = child_mm;
    task->exit_code = 0;
    task->exit_state = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    set_task_comm(task, parent->comm);
    task->fs.pwd = parent->fs.pwd;
    if (task->fs.pwd) task->fs.pwd->refcount++;
    task->fs.root = parent->fs.root;
    if (task->fs.root) task->fs.root->refcount++;

    task->uid = parent->uid;
    task->gid = parent->gid;
    task->umask = parent->umask;
    task->nameidata = NULL;
    files_init(task);
    files_clone(task, parent);
    task->uid = parent->uid;
    task->gid = parent->gid;
    task->umask = parent->umask;
    task->waitq = NULL;
    init_waitqueue_entry(&task->wait_entry, task);
    INIT_LIST_HEAD(&task->tasks);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sibling);
    init_waitqueue_head(&task->child_exit_wait);

    uint32_t flags = tasklist_lock();
    list_add_tail(&task->sibling, &parent->children);
    list_add_tail(&task->tasks, &task_list_head);
    tasklist_unlock(flags);

    return (int)task->pid;
}

/* task_create_internal: Implement task create internal. */
static int task_create_internal(void (*entry)(void), const char *program)
{
    struct task_struct *parent = task_current();
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);

    if (!task || !stack) {
        printk("TASK: Failed to create task.\n");
        return -1;
    }

    atomic_set(&task->usage, 1);
    task->pid = ++last_pid;
    task->parent = parent;
    task->thread.regs = copy_thread(stack, entry, NULL);
    task->thread.sp0 = (uint32_t*)((uint32_t)stack + THREAD_SIZE);
    task->wake_jiffies = 0;
    task->state = TASK_RUNNABLE;
    task->time_slice = TASK_TIMESLICE_TICKS;
    task->mm = NULL;
    if (program) {
        task->mm = mm_create();
        if (!task->mm) {
            kfree(stack);
            kfree(task);
            return -1;
        }
    }
    task->exit_code = 0;
    task->exit_state = 0;
    task->exit_info0 = 0;
    task->exit_info1 = 0;
    task->uid = parent ? parent->uid : 0;
    task->gid = parent ? parent->gid : 0;
    task->umask = parent ? parent->umask : 022;
    set_task_comm(task, program);
    task->nameidata = NULL;
    if (parent) {
        task->fs.pwd = parent->fs.pwd;
        task->fs.root = parent->fs.root;
        if (task->fs.pwd)
            task->fs.pwd->refcount++;
        if (task->fs.root)
            task->fs.root->refcount++;
    } else {
        task->fs.pwd = vfs_root_dentry;
        task->fs.root = vfs_root_dentry;
        if (task->fs.pwd)
            task->fs.pwd->refcount++;
        if (task->fs.root)
            task->fs.root->refcount++;
    }
    files_init(task);
    task->waitq = NULL;
    init_waitqueue_entry(&task->wait_entry, task);
    INIT_LIST_HEAD(&task->tasks);
    INIT_LIST_HEAD(&task->children);
    INIT_LIST_HEAD(&task->sibling);
    init_waitqueue_head(&task->child_exit_wait);

    uint32_t flags = tasklist_lock();
    if (parent)
        list_add_tail(&task->sibling, &parent->children);
    list_add_tail(&task->tasks, &task_list_head);
    tasklist_unlock(flags);

    return (int)task->pid;
}

/* kernel_thread: Implement kernel thread. */
int kernel_thread(void (*entry)(void))
{
    return task_create_internal(entry, NULL);
}

/* kernel_create_user: Implement kernel create user. */
int kernel_create_user(const char *program)
{
    return task_create_internal(user_task, program);
}

/* fork_init: Fork init. */
void fork_init(void)
{
    if (!task_current() || list_empty(&task_list_head))
        panic("fork_init before sched_init.");
}
