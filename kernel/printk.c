#include <stdarg.h>
#include <stdint.h>

#include "linux/console.h"
#include "linux/libc.h"
#include "linux/printk.h"

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

static void printk_puts(const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        printk_putc(*s++);
}

static void printk_pad(int n, char pad)
{
    for (int i = 0; i < n; i++)
        printk_putc(pad);
}

static int printk_strlen(const char *s)
{
    int n = 0;
    if (!s)
        return 6;
    while (s[n])
        n++;
    return n;
}

static void printk_u32_hex(uint32_t v, int width, int upper, char pad)
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
        printk_pad(width - len, pad);
    for (int i = len - 1; i >= 0; i--)
        printk_putc(tmp[i]);
}

static void printk_u32_dec(uint32_t v, int width, char pad)
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
        printk_pad(width - len, pad);
    for (int i = len - 1; i >= 0; i--)
        printk_putc(tmp[i]);
}

static void printk_s32_dec(int32_t v, int width, char pad)
{
    if (v < 0) {
        uint32_t uv = (uint32_t)(-v);
        if (pad == '0') {
            printk_putc('-');
            if (width > 0)
                width--;
            printk_u32_dec(uv, width, pad);
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
                printk_pad(width - len, pad);
            printk_putc('-');
            printk_u32_dec(uv, 0, '0');
        }
        return;
    }
    printk_u32_dec((uint32_t)v, width, pad);
}

int vprintk(const char *format, va_list args)
{
    if (!format)
        return 0;

    if (format[0] == '<' && format[1] >= '0' && format[1] <= '7' && format[2] == '>')
        format += 3;

    for (int i = 0; format[i] != 0; i++) {
        if (format[i] != '%') {
            printk_putc(format[i]);
            continue;
        }
        i++;
        if (format[i] == 0)
            break;
        if (format[i] == '%') {
            printk_putc('%');
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
                printk_s32_dec(v, width, pad);
                break;
            }
            case 'u': {
                uint32_t v = va_arg(args, uint32_t);
                printk_u32_dec(v, width, pad);
                break;
            }
            case 'x': {
                uint32_t v = va_arg(args, uint32_t);
                printk_u32_hex(v, width, 0, pad);
                break;
            }
            case 'X': {
                uint32_t v = va_arg(args, uint32_t);
                printk_u32_hex(v, width, 1, pad);
                break;
            }
            case 'p': {
                uintptr_t p = (uintptr_t)va_arg(args, void*);
                printk_puts("0x");
                printk_u32_hex((uint32_t)p, (width > 0) ? width : 8, 0, '0');
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                printk_putc(c);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char*);
                int slen = printk_strlen(s);
                if (width > slen)
                    printk_pad(width - slen, pad);
                printk_puts(s);
                break;
            }
            default:
                printk_putc('%');
                printk_putc(spec);
                break;
        }
    }
    return 0;
}

int printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vprintk(format, args);
    va_end(args);
    return ret;
}
