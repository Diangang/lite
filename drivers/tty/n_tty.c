#include "linux/tty_ldisc.h"
#include "linux/tty.h"
#include "linux/irqflags.h"
#include "linux/wait.h"
#include "linux/sched.h"
#include "linux/signal.h"
#include "linux/libc.h"

#define N_TTY_INPUT_BUF_SIZE 256

static char input_buffer[N_TTY_INPUT_BUF_SIZE];
static uint32_t input_head;
static uint32_t input_tail;
static uint32_t input_count;
static wait_queue_t input_waitq;

static char tty_linebuf[256];
static uint32_t tty_line_len;
static uint32_t tty_line_pos;

static char n_tty_getchar_blocking(void)
{
    for (;;) {
        uint32_t flags = irq_save();
        if (input_count > 0) {
            char c = input_buffer[input_tail];
            input_tail = (input_tail + 1) % N_TTY_INPUT_BUF_SIZE;
            input_count--;
            irq_restore(flags);
            return c;
        }
        wait_queue_block_locked(&input_waitq);
        irq_restore(flags);
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

static void n_tty_receive_char(char c)
{
    if (c == '\r')
        c = '\n';

    uint32_t flags = irq_save();
    if (input_count < N_TTY_INPUT_BUF_SIZE) {
        input_buffer[input_head] = c;
        input_head = (input_head + 1) % N_TTY_INPUT_BUF_SIZE;
        input_count++;
        wake_up_all(&input_waitq);
    }
    irq_restore(flags);
}

static uint32_t n_tty_read(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;

    uint32_t read = 0;
    for (;;) {
        uint32_t tty_flags = tty_get_flags();
        if (tty_flags & TTY_FLAG_CANON) {
            if (tty_line_pos < tty_line_len) {
                while (read < len && tty_line_pos < tty_line_len)
                    buf[read++] = tty_linebuf[tty_line_pos++];
                if (tty_line_pos >= tty_line_len) {
                    tty_line_len = 0;
                    tty_line_pos = 0;
                }
                return read;
            }

            tty_line_len = 0;
            tty_line_pos = 0;
            for (;;) {
                char c = n_tty_getchar_blocking();
                if (c == 0x03) {
                    n_tty_handle_ctrl_c();
                    return 0;
                }
                if (c == '\r')
                    c = '\n';
                if (c == '\n') {
                    if (tty_line_len + 1 < sizeof(tty_linebuf))
                        tty_linebuf[tty_line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO)
                        tty_put_char('\n');
                    break;
                }
                if (c == '\b' || c == 0x7F) {
                    if (tty_line_len > 0) {
                        tty_line_len--;
                        if (tty_flags & TTY_FLAG_ECHO) {
                            tty_put_char('\b');
                            tty_put_char(' ');
                            tty_put_char('\b');
                        }
                    }
                    continue;
                }
                if (tty_line_len + 1 < sizeof(tty_linebuf)) {
                    tty_linebuf[tty_line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO)
                        tty_put_char(c);
                }
            }
        } else {
            char c = n_tty_getchar_blocking();
            if (c == 0x03) {
                n_tty_handle_ctrl_c();
                return 0;
            }
            buf[read++] = c;
            return read;
        }
    }
}

void n_tty_init(void)
{
    input_head = input_tail = input_count = 0;
    tty_line_len = 0;
    tty_line_pos = 0;
    init_waitqueue_head(&input_waitq);
}

const struct tty_ldisc_ops n_tty_ldisc_ops = {
    .name = "n_tty",
    .receive_char = n_tty_receive_char,
    .read = n_tty_read,
};
