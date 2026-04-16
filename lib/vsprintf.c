#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "linux/vsprintf.h"

struct vsn_ctx {
    char *buf;
    size_t size;   /* includes space for trailing NUL */
    size_t pos;    /* bytes actually written (excluding NUL) */
    size_t total;  /* bytes that would have been written (excluding NUL) */
};

static void vsn_emit_ch(struct vsn_ctx *ctx, char c)
{
    if (!ctx)
        return;
    if (ctx->buf && ctx->size > 0 && ctx->pos + 1 < ctx->size)
        ctx->buf[ctx->pos] = c;
    if (ctx->buf && ctx->size > 0 && ctx->pos + 1 < ctx->size)
        ctx->pos++;
    ctx->total++;
}

static void vsn_emit_str(struct vsn_ctx *ctx, const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        vsn_emit_ch(ctx, *s++);
}

static int vsn_strlen(const char *s)
{
    int n = 0;
    if (!s)
        return 6;
    while (s[n])
        n++;
    return n;
}

static void vsn_emit_pad(struct vsn_ctx *ctx, int n, char pad)
{
    for (int i = 0; i < n; i++)
        vsn_emit_ch(ctx, pad);
}

static void vsn_emit_u32_hex(struct vsn_ctx *ctx, uint32_t v, int width, int upper, char pad)
{
    char tmp[16];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v && len < (int)sizeof(tmp)) {
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
        vsn_emit_pad(ctx, width - len, pad);
    for (int i = len - 1; i >= 0; i--)
        vsn_emit_ch(ctx, tmp[i]);
}

static void vsn_emit_u32_dec(struct vsn_ctx *ctx, uint32_t v, int width, char pad)
{
    char tmp[16];
    int len = 0;
    if (v == 0) {
        tmp[len++] = '0';
    } else {
        while (v && len < (int)sizeof(tmp)) {
            uint32_t d = v % 10;
            tmp[len++] = (char)('0' + d);
            v /= 10;
        }
    }
    if (width > len)
        vsn_emit_pad(ctx, width - len, pad);
    for (int i = len - 1; i >= 0; i--)
        vsn_emit_ch(ctx, tmp[i]);
}

static void vsn_emit_s32_dec(struct vsn_ctx *ctx, int32_t v, int width, char pad)
{
    if (v < 0) {
        uint32_t uv = (uint32_t)(-v);
        if (pad == '0') {
            vsn_emit_ch(ctx, '-');
            if (width > 0)
                width--;
            vsn_emit_u32_dec(ctx, uv, width, pad);
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
                vsn_emit_pad(ctx, width - len, pad);
            vsn_emit_ch(ctx, '-');
            vsn_emit_u32_dec(ctx, uv, 0, '0');
        }
        return;
    }
    vsn_emit_u32_dec(ctx, (uint32_t)v, width, pad);
}

int vsnprintf(char *buf, size_t size, const char *format, va_list args)
{
    struct vsn_ctx ctx;
    ctx.buf = buf;
    ctx.size = size;
    ctx.pos = 0;
    ctx.total = 0;

    if (!format) {
        if (buf && size > 0)
            buf[0] = 0;
        return 0;
    }

    /* printk-style "<0>" priority prefix: ignore it for formatting. */
    if (format[0] == '<' && format[1] >= '0' && format[1] <= '7' && format[2] == '>')
        format += 3;

    for (int i = 0; format[i] != 0; i++) {
        if (format[i] != '%') {
            vsn_emit_ch(&ctx, format[i]);
            continue;
        }
        i++;
        if (format[i] == 0)
            break;
        if (format[i] == '%') {
            vsn_emit_ch(&ctx, '%');
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
                vsn_emit_s32_dec(&ctx, v, width, pad);
                break;
            }
            case 'u': {
                uint32_t v = va_arg(args, uint32_t);
                vsn_emit_u32_dec(&ctx, v, width, pad);
                break;
            }
            case 'x': {
                uint32_t v = va_arg(args, uint32_t);
                vsn_emit_u32_hex(&ctx, v, width, 0, pad);
                break;
            }
            case 'X': {
                uint32_t v = va_arg(args, uint32_t);
                vsn_emit_u32_hex(&ctx, v, width, 1, pad);
                break;
            }
            case 'p': {
                uintptr_t p = (uintptr_t)va_arg(args, void *);
                vsn_emit_str(&ctx, "0x");
                vsn_emit_u32_hex(&ctx, (uint32_t)p, (width > 0) ? width : 8, 0, '0');
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                vsn_emit_ch(&ctx, c);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                int slen = vsn_strlen(s);
                if (width > slen)
                    vsn_emit_pad(&ctx, width - slen, pad);
                vsn_emit_str(&ctx, s);
                break;
            }
            default:
                vsn_emit_ch(&ctx, '%');
                vsn_emit_ch(&ctx, spec);
                break;
        }
    }

    if (buf && size > 0) {
        size_t term = (ctx.pos < size) ? ctx.pos : (size - 1);
        buf[term] = 0;
    }
    return (int)ctx.total;
}

int snprintf(char *buf, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, size, format, args);
    va_end(args);
    return n;
}

