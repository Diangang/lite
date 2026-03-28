#include <stdarg.h>

#include "linux/console.h"
#include "linux/libc.h"
#include "linux/printk.h"

int printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    char buf[32];

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%' && format[i + 1] != '\0') {
            i++;
            switch (format[i]) {
                case 'd': {
                    int val = va_arg(args, int);
                    itoa(val, 10, buf);
                    printk(buf);
                    break;
                }
                case 'x': {
                    int val = va_arg(args, int);
                    itoa(val, 16, buf);
                    printk(buf);
                    break;
                }
                case 's': {
                    char *str = va_arg(args, char*);
                    printk(str);
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    console_put_char(c);
                    break;
                }
                default:
                    console_put_char('%');
                    console_put_char(format[i]);
                    break;
            }
        } else {
            console_put_char(format[i]);
        }
    }

    va_end(args);
    return 0;
}
