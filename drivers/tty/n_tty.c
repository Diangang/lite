#include "linux/tty_ldisc.h"
#include "linux/tty.h"
#include "linux/irqflags.h"
#include "linux/wait.h"
#include "linux/sched.h"
#include "linux/signal.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/slab.h"

#define N_TTY_INPUT_BUF_SIZE 256

struct n_tty_data {
    char input_buffer[N_TTY_INPUT_BUF_SIZE];
    uint32_t input_head;
    uint32_t input_tail;
    uint32_t input_count;
    wait_queue_head_t input_waitq;
    char linebuf[256];
    uint32_t line_len;
    uint32_t line_pos;
};

static struct n_tty_data *n_tty_data(struct tty_struct *tty)
{
    if (!tty)
        return NULL;
    return (struct n_tty_data *)tty->disc_data;
}

static int n_tty_open(struct tty_struct *tty)
{
    struct n_tty_data *ldata;

    if (!tty)
        return -1;
    if (tty->disc_data)
        return 0;
    ldata = kmalloc(sizeof(*ldata));
    if (!ldata)
        return -1;
    memset(ldata, 0, sizeof(*ldata));
    init_waitqueue_head(&ldata->input_waitq);
    tty->disc_data = ldata;
    return 0;
}

static void n_tty_close(struct tty_struct *tty)
{
    struct n_tty_data *ldata = n_tty_data(tty);

    if (!ldata)
        return;
    tty->disc_data = NULL;
    kfree(ldata);
}

static char n_tty_getchar_blocking(struct tty_struct *tty, struct n_tty_data *ldata)
{
    for (;;) {
        uint32_t flags = irq_save();
        if (ldata->input_count > 0) {
            char c = ldata->input_buffer[ldata->input_tail];
            ldata->input_tail = (ldata->input_tail + 1) % N_TTY_INPUT_BUF_SIZE;
            ldata->input_count--;
            irq_restore(flags);
            return c;
        }
        wait_queue_block_locked(&ldata->input_waitq);
        irq_restore(flags);
        (void)tty;
        task_yield();
    }
}

static int n_tty_handle_ctrl_c(void)
{
    uint32_t pid = tty_get_foreground_pid();
    if (pid != 0) {
        sys_kill(pid, SIGINT);
        tty_set_foreground_pid(0);
        tty_user_exit_hook_call();
    }
    printf("^C\n");
    return 1;
}

static void n_tty_receive_buf(struct tty_struct *tty, const uint8_t *buf, uint32_t len)
{
    struct n_tty_data *ldata = n_tty_data(tty);

    if (!buf || len == 0)
        return;
    if (!ldata)
        return;

    uint32_t flags = irq_save();
    for (uint32_t i = 0; i < len; i++) {
        char c = (char)buf[i];
        if (c == '\r')
            c = '\n';
        if (ldata->input_count < N_TTY_INPUT_BUF_SIZE) {
            ldata->input_buffer[ldata->input_head] = c;
            ldata->input_head = (ldata->input_head + 1) % N_TTY_INPUT_BUF_SIZE;
            ldata->input_count++;
        }
    }
    wake_up_all(&ldata->input_waitq);
    irq_restore(flags);
}

static uint32_t n_tty_read(struct tty_struct *tty, char *buf, uint32_t len)
{
    struct n_tty_data *ldata = n_tty_data(tty);

    if (!buf || len == 0)
        return 0;
    if (!ldata)
        return 0;

    uint32_t read = 0;
    for (;;) {
        uint32_t tty_flags = tty_get_flags();
        if (tty_flags & TTY_FLAG_CANON) {
            if (ldata->line_pos < ldata->line_len) {
                while (read < len && ldata->line_pos < ldata->line_len)
                    buf[read++] = ldata->linebuf[ldata->line_pos++];
                if (ldata->line_pos >= ldata->line_len) {
                    ldata->line_len = 0;
                    ldata->line_pos = 0;
                }
                return read;
            }

            ldata->line_len = 0;
            ldata->line_pos = 0;
            for (;;) {
                char c = n_tty_getchar_blocking(tty, ldata);
                if (c == 0x03) {
                    n_tty_handle_ctrl_c();
                    return 0;
                }
                if (c == '\r')
                    c = '\n';
                if (c == '\n') {
                    if (ldata->line_len + 1 < sizeof(ldata->linebuf))
                        ldata->linebuf[ldata->line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO)
                        tty_put_char('\n');
                    break;
                }
                if (c == '\b' || c == 0x7F) {
                    if (ldata->line_len > 0) {
                        ldata->line_len--;
                        if (tty_flags & TTY_FLAG_ECHO) {
                            tty_put_char('\b');
                            tty_put_char(' ');
                            tty_put_char('\b');
                        }
                    }
                    continue;
                }
                if (ldata->line_len + 1 < sizeof(ldata->linebuf)) {
                    ldata->linebuf[ldata->line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO)
                        tty_put_char(c);
                }
            }
        } else {
            char c = n_tty_getchar_blocking(tty, ldata);
            if (c == 0x03) {
                n_tty_handle_ctrl_c();
                return 0;
            }
            buf[read++] = c;
            return read;
        }
    }
}

const struct tty_ldisc_ops n_tty_ldisc_ops = {
    .name = "n_tty",
    .open = n_tty_open,
    .close = n_tty_close,
    .receive_buf = n_tty_receive_buf,
    .read = n_tty_read,
};
