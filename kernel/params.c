#include "linux/io.h"
#include "linux/string.h"
#include "linux/kernel.h"
#include "linux/printk.h"
#include "linux/init.h"

/* set_execute_command: Set init command override. */
char saved_command_line[256];
static char execute_command[64];

static void set_execute_command(const char *value, size_t len)
{
    if (len >= sizeof(execute_command))
        len = sizeof(execute_command) - 1;
    if (len)
        memcpy(execute_command, value, len);
    execute_command[len] = '\0';
}

/* parse_command_line: Parse command line. */
static void parse_command_line(void)
{
    size_t i = 0;

    strcpy(execute_command, "/sbin/init");

    while (saved_command_line[i]) {
        while (saved_command_line[i] == ' ')
            i++;
        if (!saved_command_line[i])
            break;
        if (!strncmp(&saved_command_line[i], "init=", 5)) {
            size_t start = i + 5;
            size_t end = start;
            while (saved_command_line[end] && saved_command_line[end] != ' ')
                end++;
            set_execute_command(&saved_command_line[start], end - start);
            return;
        }
        while (saved_command_line[i] && saved_command_line[i] != ' ')
            i++;
    }
}

/* setup_command_line: Set up command line. */
void setup_command_line(const char *cmdline)
{
    size_t len = 0;

    if (cmdline)
        len = strlen(cmdline);

    if (len >= sizeof(saved_command_line))
        len = sizeof(saved_command_line) - 1;

    if (len)
        memcpy(saved_command_line, cmdline, len);

    saved_command_line[len] = '\0';
    parse_command_line();
}

/* get_execute_command: Get init command override. */
const char *get_execute_command(void)
{
    return execute_command;
}

/*
 * get_cmdline_param: Return 0 and copy value when key=value exists.
 * If key appears without "=value", returns 0 with empty value string.
 * Returns -1 when key is absent.
 */
int get_cmdline_param(const char *key, char *value, size_t cap)
{
    if (!key || !key[0] || !value || cap == 0)
        return -1;

    value[0] = '\0';

    size_t key_len = strlen(key);
    size_t i = 0;
    while (saved_command_line[i]) {
        while (saved_command_line[i] == ' ')
            i++;
        if (!saved_command_line[i])
            break;

        size_t start = i;
        while (saved_command_line[i] && saved_command_line[i] != ' ')
            i++;
        size_t end = i;

        if (end > start + key_len &&
            !strncmp(&saved_command_line[start], key, key_len) &&
            saved_command_line[start + key_len] == '=') {
            size_t vstart = start + key_len + 1;
            size_t vlen = end - vstart;
            if (vlen >= cap)
                vlen = cap - 1;
            if (vlen)
                memcpy(value, &saved_command_line[vstart], vlen);
            value[vlen] = '\0';
            return 0;
        }
        if (end == start + key_len &&
            !strncmp(&saved_command_line[start], key, key_len)) {
            value[0] = '\0';
            return 0;
        }
    }
    return -1;
}
