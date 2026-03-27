#include "task_internal.h"
#include "kheap.h"
#include "libc.h"
#include "vmm.h"
#include "pmm.h"
#include "devfs.h"
#include "fs.h"
#include "console.h"

static struct vm_area_struct *vma_clone_list(struct vm_area_struct *src)
{
    struct vm_area_struct *head = NULL;
    struct vm_area_struct **tail = &head;
    while (src) {
        struct vm_area_struct *v = (struct vm_area_struct*)kmalloc(sizeof(struct vm_area_struct));
        if (!v) {
            struct vm_area_struct *n = head;
            while (n) {
                struct vm_area_struct *next = n->vm_next;
                kfree(n);
                n = next;
            }
            return NULL;
        }
        v->vm_start = src->vm_start;
        v->vm_end = src->vm_end;
        v->vm_flags = src->vm_flags;
        v->vm_next = NULL;
        *tail = v;
        tail = &v->vm_next;
        src = src->vm_next;
    }
    return head;
}

static struct mm_struct *mm_clone_cow(struct mm_struct *src)
{
    if (!src)
        return NULL;
    uint32_t *new_dir = vmm_clone_kernel_directory();
    if (!new_dir)
        return NULL;
    struct mm_struct *mm = (struct mm_struct*)kmalloc(sizeof(struct mm_struct));
    if (!mm) {
        pmm_free_page(new_dir);
        return NULL;
    }
    memset(mm, 0, sizeof(*mm));
    mm->pgd = new_dir;
    mm->start_code = src->start_code;
    mm->end_code = src->end_code;
    mm->start_stack = src->start_stack;
    mm->start_brk = src->start_brk;
    mm->brk = src->brk;
    mm->mmap = vma_clone_list(src->mmap);
    if (src->mmap && !mm->mmap) {
        pmm_free_page(new_dir);
        kfree(mm);
        return NULL;
    }
    struct vm_area_struct *v = src->mmap;
    while (v) {
        uint32_t start = v->vm_start & ~0xFFF;
        uint32_t end = (v->vm_end + 0xFFF) & ~0xFFF;
        for (uint32_t va = start; va < end; va += 4096) {
            uint32_t pte = vmm_get_pte_flags_ex(src->pgd, (void*)va);
            if (!(pte & PTE_PRESENT))
                continue;
            if (!(pte & PTE_USER))
                continue;
            uint32_t phys = pte & ~0xFFF;
            uint32_t flags = pte & 0xFFF;
            int was_write = (flags & PTE_READ_WRITE) != 0;
            if (was_write) {
                flags &= ~PTE_READ_WRITE;
                flags |= PTE_COW;
                vmm_update_page_flags_ex(src->pgd, (void*)va, flags);
            }
            vmm_map_page_ex(new_dir, (void*)phys, (void*)va, flags);
            pmm_ref_page((void*)phys);
        }
        v = v->vm_next;
    }
    return mm;
}

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

void task_set_comm(struct task_struct *task, const char *program)
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

int task_fork(struct pt_regs *regs)
{
    if (!current || !current->mm || !regs)
        return -1;
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);
    if (!task || !stack) {
        if (task) kfree(task);
        if (stack) kfree(stack);
        return -1;
    }

    struct mm_struct *child_mm = mm_clone_cow(current->mm);
    if (!child_mm) {
        kfree(stack);
        kfree(task);
        return -1;
    }

    task->pid = next_task_id++;
    task->parent = current;
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
    task_set_comm(task, current->comm);
    task->fs.pwd = current->fs.pwd;
    if (task->fs.pwd) task->fs.pwd->refcount++;
    task->fs.root = current->fs.root;
    if (task->fs.root) task->fs.root->refcount++;

    if (current) {
        task->uid = current->uid;
        task->gid = current->gid;
        task->umask = current->umask;
    } else {
        task->uid = 0;
        task->gid = 0;
        task->umask = 022;
    }
    task_fdtable_init(task);
    task_fdtable_clone(task, current);
    task->uid = current->uid;
    task->gid = current->gid;
    task->umask = current->umask;
    task->waitq = NULL;
    task->wait_next = NULL;

    uint32_t flags = irq_save();
    task->next = task_head->next;
    task_head->next = task;
    irq_restore(flags);

    return (int)task->pid;
}

static int task_create_internal(void (*entry)(void), const char *program)
{
    struct task_struct *task = (struct task_struct*)kmalloc(sizeof(struct task_struct));
    uint32_t *stack = (uint32_t*)kmalloc(THREAD_SIZE);

    if (!task || !stack)
        return printf("TASK: Failed to create task.\n"), -1;

    task->pid = next_task_id++;
    task->parent = current;
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
    task->uid = current ? current->uid : 0;
    task->gid = current ? current->gid : 0;
    task->umask = current ? current->umask : 022;
    task_set_comm(task, program);
    if (current) {
        task->fs.pwd = current->fs.pwd;
        task->fs.root = current->fs.root;
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
    task_fdtable_init(task);
    if (program) {
        struct task_struct *prev = current;
        current = task;
        task_install_stdio(devfs_get_console());
        current = prev;
    }
    task->waitq = NULL;
    task->wait_next = NULL;

    uint32_t flags = irq_save();
    task->next = task_head->next;
    task_head->next = task;
    irq_restore(flags);

    return (int)task->pid;
}

int task_create(void (*entry)(void))
{
    return task_create_internal(entry, NULL);
}

int task_create_user(const char *program)
{
    return task_create_internal(user_task, program);
}
