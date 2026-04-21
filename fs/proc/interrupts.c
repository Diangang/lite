// fs/proc/interrupts.c - Lite procfs /proc/irq (Linux mapping: fs/proc/interrupts.c)

#include <stdint.h>

#include "linux/fs.h"
#include "linux/file.h"
#include "linux/interrupt.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"

#include "internal.h"

/* proc_read_irq: Implement proc read IRQ. */
static uint32_t proc_read_irq(struct inode *node, uint32_t offset, uint32_t size, uint8_t *buffer)
{
    (void)node;

    static char tmp[1024];
    uint32_t n = 0;

    snprintf(tmp, sizeof(tmp),
             "irq0=%u\nirq1=%u\nirq4=%u\nsyscall128=%u\n",
             irq_get_count(IRQ_TIMER),
             irq_get_count(IRQ_KEYBOARD),
             irq_get_count(IRQ_COM1),
             isr_get_count(128));
    n = (uint32_t)strlen(tmp);

    if (offset >= n)
        return 0;
    uint32_t remain = n - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, tmp + offset, size);
    return size;
}

static struct file_operations proc_irq_ops = {
    .read = proc_read_irq,
    .write = NULL,
    .open = NULL,
    .close = NULL,
    .readdir = NULL,
    .ioctl = NULL
};

static struct inode proc_irq;

void proc_interrupts_init(void)
{
    memset(&proc_irq, 0, sizeof(proc_irq));
    proc_irq.flags = FS_FILE;
    proc_irq.i_ino = 4;
    proc_irq.i_size = 1024;
    proc_irq.f_ops = &proc_irq_ops;
    proc_irq.uid = 0;
    proc_irq.gid = 0;
    proc_irq.i_mode = 0444;

    proc_register_root_child("irq", &proc_irq);
}

