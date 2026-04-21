#ifndef LINUX_TTY_LDISC_H
#define LINUX_TTY_LDISC_H

#include <stdint.h>

/*
 * Minimal tty line discipline boundary (Linux mapping):
 * - Linux has struct tty_ldisc_ops (drivers/tty/n_tty.c et al).
 * - Lite currently supports a single global ldisc (N_TTY subset).
 */

struct tty_struct;

struct tty_ldisc_ops {
    const char *name;
    /* Linux mapping: n_tty_open()/n_tty_close() */
    int (*open)(struct tty_struct *tty);
    void (*close)(struct tty_struct *tty);
    /* Linux mapping: n_tty_receive_buf()/receive_buf() */
    void (*receive_buf)(struct tty_struct *tty, const uint8_t *buf, uint32_t len);
    /* Legacy Lite entry (kept for compatibility during refactors). */
    void (*receive_char)(struct tty_struct *tty, char c);
    uint32_t (*read)(struct tty_struct *tty, char *buf, uint32_t len);
};

/* Linux mapping: N_TTY line discipline. */
extern const struct tty_ldisc_ops n_tty_ldisc_ops;

#endif
