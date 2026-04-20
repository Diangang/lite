#include "linux/sched.h"
#include "linux/pid.h"
#include "internal.h"

/* task_dump_maps: Implement task dump maps. */
uint32_t task_dump_maps(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (!current || !current->mm)
        return 0;

    uint32_t off = 0;
    struct vm_area_struct *v = current->mm->mmap;
    while (v) {
        proc_buf_append_hex(buf, &off, len, v->vm_start);
        proc_buf_append(buf, &off, len, "-");
        proc_buf_append_hex(buf, &off, len, v->vm_end);
        proc_buf_append(buf, &off, len, " ");
        proc_buf_append(buf, &off, len, (v->vm_flags & VMA_READ) ? "r" : "-");
        proc_buf_append(buf, &off, len, (v->vm_flags & VMA_WRITE) ? "w" : "-");
        proc_buf_append(buf, &off, len, (v->vm_flags & VMA_EXEC) ? "x" : "-");
        proc_buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        v = v->vm_next;
    }

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

/* task_dump_maps_pid: Implement task dump maps pid. */
uint32_t task_dump_maps_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (list_empty(&task_list_head))
        return 0;

    struct task_struct *t = find_task_by_vpid(pid);
    if (!t || !t->mm)
        return 0;

    uint32_t off = 0;
    struct vm_area_struct *v = t->mm->mmap;
    while (v) {
        proc_buf_append_hex(buf, &off, len, v->vm_start);
        proc_buf_append(buf, &off, len, "-");
        proc_buf_append_hex(buf, &off, len, v->vm_end);
        proc_buf_append(buf, &off, len, " ");
        proc_buf_append(buf, &off, len, (v->vm_flags & VMA_READ) ? "r" : "-");
        proc_buf_append(buf, &off, len, (v->vm_flags & VMA_WRITE) ? "w" : "-");
        proc_buf_append(buf, &off, len, (v->vm_flags & VMA_EXEC) ? "x" : "-");
        proc_buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
        v = v->vm_next;
    }

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}
