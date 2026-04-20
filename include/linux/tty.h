#ifndef LINUX_TTY_H
#define LINUX_TTY_H

#include <stdint.h>
#include "linux/list.h"

struct device;
struct device_type;

struct tty_driver {
    char name[32];
    uint32_t num;
    struct list_head list;
};

/* Linux-compatible naming: per-line port/device representation. */
struct tty_port {
    struct tty_driver *driver;
    uint32_t index;
    char name[32];
    uint32_t major;
    uint32_t minor;
    void *driver_data;
};

/*
 * Minimal tty_struct for Linux-aligned ldisc signatures.
 *
 * Linux mapping: include/linux/tty.h struct tty_struct.
 * Lite simplification: single global tty_struct is sufficient for n_tty.
 */
struct tty_struct {
    struct tty_port *port;
    void *disc_data;
};

#define TTY_FLAG_ECHO  0x1
#define TTY_FLAG_CANON 0x2

enum tty_output_target {
    TTY_OUTPUT_SERIAL = 1 << 0,
    TTY_OUTPUT_ALL = TTY_OUTPUT_SERIAL
};

void tty_set_user_exit_hook(void (*hook)(void));
void tty_user_exit_hook_call(void);
void tty_set_foreground_pid(uint32_t pid);
uint32_t tty_get_foreground_pid(void);
uint32_t tty_get_flags(void);
void tty_set_flags(uint32_t flags);
void tty_receive_char(char c);
uint32_t tty_read_blocking(char *buf, uint32_t len);
void tty_set_output_targets(uint32_t targets);
uint32_t tty_get_output_targets(void);
void tty_put_char(char c);
uint32_t tty_write(const uint8_t *buf, uint32_t len);
int tty_register_driver(struct tty_driver *drv, const char *name, uint32_t num);
struct device *tty_register_device(struct tty_driver *drv, uint32_t index, struct device *parent, const char *name, void *data);
struct tty_port *tty_port_from_dev(struct device *dev);

#endif
