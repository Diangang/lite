#include "linux/libc.h"
#include "linux/params.h"

char saved_command_line[256];
static char init_process[64];

static void set_init_process(const char *value, size_t len)
{
    if (len >= sizeof(init_process))
        len = sizeof(init_process) - 1;
    if (len)
        memcpy(init_process, value, len);
    init_process[len] = '\0';
}

static void parse_command_line(void)
{
    size_t i = 0;

    strcpy(init_process, "/sbin/init");

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
            set_init_process(&saved_command_line[start], end - start);
            return;
        }
        while (saved_command_line[i] && saved_command_line[i] != ' ')
            i++;
    }
}

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

const char *get_init_process(void)
{
    return init_process;
}
