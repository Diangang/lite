#include "linux/sched.h"
#include "linux/pid.h"
#include "linux/sched.h"
#include "internal.h"

/* task_dump_tasks: Implement task dump tasks. */
uint32_t task_dump_tasks(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    if (list_empty(&task_list_head))
        return 0;

    uint32_t off = 0;
    proc_buf_append(buf, &off, len, "PID   STATE     WAKE    CURRENT  NAME\n");
    struct task_struct *task;
    list_for_each_entry(task, &task_list_head, tasks) {
        const char *state = "RUNNABLE";
        if (task->state == 1)
            state = "SLEEPING";
        else if (task->state == 2)
            state = "BLOCKED";
        else if (task->state == 3)
            state = "ZOMBIE";
        const char *name = task->comm[0] ? task->comm : "-";
        proc_buf_append_u32(buf, &off, len, task->pid);
        proc_buf_append(buf, &off, len, "    ");
        proc_buf_append(buf, &off, len, state);
        proc_buf_append(buf, &off, len, "  ");
        proc_buf_append_u32(buf, &off, len, task->wake_jiffies);
        proc_buf_append(buf, &off, len, "    ");
        proc_buf_append(buf, &off, len, task == current ? "yes" : "no");
        proc_buf_append(buf, &off, len, "     ");
        proc_buf_append(buf, &off, len, name);
        proc_buf_append(buf, &off, len, "\n");
        if (off >= len)
            break;
    }

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

/* task_dump_stat_pid: Implement task dump stat pid. */
uint32_t task_dump_stat_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_vpid(pid);
    if (!t)
        return 0;

    uint32_t off = 0;
    char name_buf[sizeof(t->comm)];
    const char *name = get_task_comm(name_buf, t);
    if (!name[0])
        name = "-";
    const char *st = "R";
    if (t->state == 1)
        st = "S";
    else if (t->state == 2)
        st = "D";
    else if (t->state == 3)
        st = "Z";
    proc_buf_append_u32(buf, &off, len, task_pid_nr(t));
    proc_buf_append(buf, &off, len, " (");
    proc_buf_append(buf, &off, len, name);
    proc_buf_append(buf, &off, len, ") ");
    proc_buf_append(buf, &off, len, st);
    proc_buf_append(buf, &off, len, " ");
    proc_buf_append_u32(buf, &off, len, t->wake_jiffies);
    proc_buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

/* task_dump_cmdline_pid: Implement task dump cmdline pid. */
uint32_t task_dump_cmdline_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_vpid(pid);
    if (!t)
        return 0;

    uint32_t off = 0;
    const char *name = t->comm[0] ? t->comm : "-";
    proc_buf_append(buf, &off, len, name);
    proc_buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}

/* task_dump_status_pid: Implement task dump status pid. */
uint32_t task_dump_status_pid(uint32_t pid, char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    struct task_struct *t = find_task_by_vpid(pid);
    if (!t)
        return 0;

    char name_buf[sizeof(t->comm)];
    const char *name = get_task_comm(name_buf, t);
    if (!name[0])
        name = "-";
    const char *state = "RUNNABLE";
    if (t->state == 1)
        state = "SLEEPING";
    else if (t->state == 2)
        state = "BLOCKED";
    else if (t->state == 3)
        state = "ZOMBIE";

    uint32_t off = 0;
    proc_buf_append(buf, &off, len, "Name:\t");
    proc_buf_append(buf, &off, len, name);
    proc_buf_append(buf, &off, len, "\nState:\t");
    proc_buf_append(buf, &off, len, state);
    proc_buf_append(buf, &off, len, "\nTgid:\t");
    proc_buf_append_u32(buf, &off, len, task_tgid_nr(t));
    proc_buf_append(buf, &off, len, "\nPid:\t");
    proc_buf_append_u32(buf, &off, len, task_pid_nr(t));
    proc_buf_append(buf, &off, len, "\nPPid:\t");
    proc_buf_append_u32(buf, &off, len, task_ppid_nr(t));
    proc_buf_append(buf, &off, len, "\nType:\t");
    proc_buf_append(buf, &off, len, t->mm ? "user" : "kthread");
    proc_buf_append(buf, &off, len, "\nCwd:\t");
    proc_buf_append(buf, &off, len, "/");
    proc_buf_append(buf, &off, len, "\nExitCode:\t");
    proc_buf_append_u32(buf, &off, len, (uint32_t)t->exit_code);
    proc_buf_append(buf, &off, len, "\nExitState:\t");
    proc_buf_append_u32(buf, &off, len, (uint32_t)t->exit_state);
    if (t->exit_state == TASK_EXIT_SIGNAL) {
        proc_buf_append(buf, &off, len, "\nSignal:\t");
        proc_buf_append_u32(buf, &off, len, t->exit_info0);
    }
    proc_buf_append(buf, &off, len, "\n");

    if (off < len)
        buf[off] = 0;
    else
        buf[len - 1] = 0;
    return off;
}
