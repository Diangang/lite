#include <stdarg.h>
#include <stdint.h>

#include "linux/console.h"
#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"

/*
 * Console core (Linux mapping: kernel/printk/printk.c register_console()).
 *
 * Lite keeps a minimal subset:
 * - A single linked list of console sinks.
 * - printk_putc() fans out to all registered sinks.
 */
static struct console *console_list;

int register_console(struct console *con)
{
    if (!con || !con->write)
        return -1;
    con->next = console_list;
    console_list = con;
    return 0;
}

void unregister_console(struct console *con)
{
    struct console **pp = &console_list;
    while (*pp) {
        if (*pp == con) {
            *pp = con->next;
            con->next = (struct console *)0;
            return;
        }
        pp = &(*pp)->next;
    }
}

uint32_t console_write(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    for (struct console *con = console_list; con; con = con->next)
        con->write(buf, len);
    return len;
}

void console_put_char(char c)
{
    unsigned char b = (unsigned char)c;
    console_write((const uint8_t *)&b, 1);
}

/* printk_putc: Implement printk putc. */
static char printk_log_buf[8192];
static uint32_t printk_log_head;
static uint32_t printk_log_count;

static void printk_putc(char c)
{
    console_put_char(c);
    printk_log_buf[printk_log_head] = c;
    printk_log_head++;
    if (printk_log_head >= sizeof(printk_log_buf))
        printk_log_head = 0;
    if (printk_log_count < sizeof(printk_log_buf))
        printk_log_count++;
}

static void printk_putc_count(char c, int *count)
{
    printk_putc(c);
    if (count)
        (*count)++;
}

/* printk_puts: Implement printk puts. */
static void printk_puts(const char *s, int *count)
{
    if (!s)
        s = "(null)";
    while (*s)
        printk_putc_count(*s++, count);
}

/* printk_pad: Implement printk pad. */
static void printk_pad(int n, char pad, int *count)
{
    for (int i = 0; i < n; i++)
        printk_putc_count(pad, count);
}

/* printk_strlen: Implement printk strlen. */
static int printk_strlen(const char *s)
{
    int n = 0;
    if (!s)
        return 6;
    while (s[n])
        n++;
    return n;
}

/* printk_u32_hex: Implement printk u32 hex. */
static void printk_u32_hex(uint32_t v, int width, int upper, char pad, int *count)
{
    char tmp[16];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v) {
            uint32_t d = v & 0xF;
            char c;
            if (d < 10)
                c = (char)('0' + d);
            else
                c = (char)((upper ? 'A' : 'a') + (d - 10));
            tmp[len++] = c;
            v >>= 4;
        }
    }
    if (width > len)
        printk_pad(width - len, pad, count);
    for (int i = len - 1; i >= 0; i--)
        printk_putc_count(tmp[i], count);
}

/* printk_u32_dec: Implement printk u32 dec. */
static void printk_u32_dec(uint32_t v, int width, char pad, int *count)
{
    char tmp[16];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v) {
            uint32_t d = v % 10;
            tmp[len++] = (char)('0' + d);
            v /= 10;
        }
    }
    if (width > len)
        printk_pad(width - len, pad, count);
    for (int i = len - 1; i >= 0; i--)
        printk_putc_count(tmp[i], count);
}

/* printk_s32_dec: Implement printk s32 dec. */
static void printk_s32_dec(int32_t v, int width, char pad, int *count)
{
    if (v < 0) {
        uint32_t uv = (uint32_t)(-v);
        if (pad == '0') {
            printk_putc_count('-', count);
            if (width > 0)
                width--;
            printk_u32_dec(uv, width, pad, count);
        } else {
            int len = 1;
            uint32_t t = uv;
            if (t == 0)
                len += 1;
            while (t) {
                len++;
                t /= 10;
            }
            if (width > len)
                printk_pad(width - len, pad, count);
            printk_putc_count('-', count);
            printk_u32_dec(uv, 0, '0', count);
        }
        return;
    }
    printk_u32_dec((uint32_t)v, width, pad, count);
}

/* vprintk: Implement vprintk. */
int vprintk(const char *format, va_list args)
{
    if (!format)
        return 0;

    if (format[0] == '<' && format[1] >= '0' && format[1] <= '7' && format[2] == '>')
        format += 3;

    int printed = 0;
    for (int i = 0; format[i] != 0; i++) {
        if (format[i] != '%') {
            printk_putc_count(format[i], &printed);
            continue;
        }
        i++;
        if (format[i] == 0)
            break;
        if (format[i] == '%') {
            printk_putc_count('%', &printed);
            continue;
        }

        char pad = ' ';
        int width = 0;
        if (format[i] == '0') {
            pad = '0';
            i++;
        }
        while (format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + (format[i] - '0');
            i++;
        }
        if (format[i] == 'l')
            i++;

        char spec = format[i];
        switch (spec) {
            case 'd': {
                int v = va_arg(args, int);
                printk_s32_dec(v, width, pad, &printed);
                break;
            }
            case 'u': {
                uint32_t v = va_arg(args, uint32_t);
                printk_u32_dec(v, width, pad, &printed);
                break;
            }
            case 'x': {
                uint32_t v = va_arg(args, uint32_t);
                printk_u32_hex(v, width, 0, pad, &printed);
                break;
            }
            case 'X': {
                uint32_t v = va_arg(args, uint32_t);
                printk_u32_hex(v, width, 1, pad, &printed);
                break;
            }
            case 'p': {
                uintptr_t p = (uintptr_t)va_arg(args, void *);
                printk_puts("0x", &printed);
                printk_u32_hex((uint32_t)p, (width > 0) ? width : 8, 0, '0', &printed);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                printk_putc_count(c, &printed);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                int slen = printk_strlen(s);
                if (width > slen)
                    printk_pad(width - slen, pad, &printed);
                printk_puts(s, &printed);
                break;
            }
            default:
                printk_putc_count('%', &printed);
                printk_putc_count(spec, &printed);
                break;
        }
    }
    return printed;
}

void printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    (void)vprintk(format, args);
    va_end(args);
}

/* printk: Implement printk. */
int printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vprintk(format, args);
    va_end(args);
    return ret;
}
