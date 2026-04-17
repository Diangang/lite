#ifndef LINUX_TTY_LDISC_H
#define LINUX_TTY_LDISC_H

#include <stdint.h>

/*
 * Minimal tty line discipline boundary (Linux mapping):
 * - Linux has struct tty_ldisc_ops (drivers/tty/n_tty.c et al).
 * - Lite currently supports a single global ldisc (N_TTY subset).
 */

struct tty_ldisc_ops {
    const char *name;
    void (*receive_char)(char c);
    uint32_t (*read)(char *buf, uint32_t len);
};

/* Linux mapping: N_TTY line discipline. */
extern const struct tty_ldisc_ops n_tty_ldisc_ops;

#endif
