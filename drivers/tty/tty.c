#include "linux/tty.h"
#include "linux/irqflags.h"
#include "linux/libc.h"
#include "linux/sched.h"
#include "linux/wait.h"
#include "linux/exit.h"
#include "linux/signal.h"
#include "linux/console.h"

#define INPUT_BUF_SIZE 256

static char input_buffer[INPUT_BUF_SIZE];
static uint32_t input_head = 0;
static uint32_t input_tail = 0;
static uint32_t input_count = 0;
static wait_queue_t input_waitq;

static uint32_t tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
static char tty_linebuf[256];
static uint32_t tty_line_len = 0;
static uint32_t tty_line_pos = 0;

static uint32_t foreground_pid = 0;
static void (*user_exit_hook)(void) = NULL;

void tty_init(void)
{
    input_head = input_tail = input_count = 0;
    tty_flags = (TTY_FLAG_ECHO | TTY_FLAG_CANON);
    tty_line_len = 0;
    tty_line_pos = 0;
    foreground_pid = 0;
    wait_queue_init(&input_waitq);
}

void tty_set_user_exit_hook(void (*hook)(void))
{
    user_exit_hook = hook;
}

void tty_set_foreground_pid(uint32_t pid)
{
    uint32_t flags = irq_save();
    foreground_pid = pid;
    irq_restore(flags);
}

uint32_t tty_get_foreground_pid(void)
{
    uint32_t flags = irq_save();
    uint32_t pid = foreground_pid;
    irq_restore(flags);
    return pid;
}

uint32_t tty_get_flags(void)
{
    return tty_flags;
}

void tty_set_flags(uint32_t flags)
{
    tty_flags = flags;
    tty_line_len = 0;
    tty_line_pos = 0;
}

void tty_receive_char(char c)
{
    if (c == '\r')
        c = '\n';

    uint32_t flags = irq_save();
    if (input_count < INPUT_BUF_SIZE) {
        input_buffer[input_head] = c;
        input_head = (input_head + 1) % INPUT_BUF_SIZE;
        input_count++;
        wait_queue_wake_all(&input_waitq);
    }
    irq_restore(flags);
}

static char tty_getchar_blocking(void)
{
    for (;;) {
        uint32_t flags = irq_save();
        if (input_count > 0) {
            char c = input_buffer[input_tail];
            input_tail = (input_tail + 1) % INPUT_BUF_SIZE;
            input_count--;
            irq_restore(flags);
            return c;
        }
        wait_queue_block_locked(&input_waitq);
        irq_restore(flags);
        task_yield();
    }
}

static int tty_handle_ctrl_c(void)
{
    uint32_t pid = tty_get_foreground_pid();
    if (pid != 0) {
        sys_kill(pid, SIGINT);
        tty_set_foreground_pid(0);
        if (user_exit_hook) user_exit_hook();
    }
    printf("^C\n");
    return 1;
}

uint32_t tty_read_blocking(char *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    uint32_t read = 0;
    for (;;) {
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
                char c = tty_getchar_blocking();
                if (c == 0x03) {
                    tty_handle_ctrl_c();
                    return 0;
                }
                if (c == '\r') c = '\n';
                if (c == '\n') {
                    if (tty_line_len + 1 < sizeof(tty_linebuf))
                        tty_linebuf[tty_line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO) printf("\n");
                    break;
                }
                if (c == '\b' || c == 0x7F) {
                    if (tty_line_len > 0) {
                        tty_line_len--;
                        if (tty_flags & TTY_FLAG_ECHO)
                            printf("\b \b");
                    }
                    continue;
                }
                if (tty_line_len + 1 < sizeof(tty_linebuf)) {
                    tty_linebuf[tty_line_len++] = c;
                    if (tty_flags & TTY_FLAG_ECHO) printf("%c", c);
                }
            }
        } else {
            char c = tty_getchar_blocking();
            if (c == 0x03) {
                tty_handle_ctrl_c();
                return 0;
            }
            if (c == '\r') c = '\n';
            buf[read++] = c;
            if (tty_flags & TTY_FLAG_ECHO) printf("%c", c);
            if (read > 0)
                return read;
        }
    }
}
