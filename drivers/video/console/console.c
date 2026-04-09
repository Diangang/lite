#include "linux/console.h"
#include "linux/libc.h"

static struct console *console_list;

uint32_t console_write(const uint8_t *buf, uint32_t len);

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

void console_put_char(char c)
{
    unsigned char b = (unsigned char)c;
    console_write((const uint8_t *)&b, 1);
}

uint32_t console_write(const uint8_t *buf, uint32_t len)
{
    if (!buf || len == 0)
        return 0;
    for (struct console *con = console_list; con; con = con->next)
        con->write(buf, len);
    return len;
}
