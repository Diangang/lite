#ifndef DRIVERS_TTY_SERIAL_8250_8250_H
#define DRIVERS_TTY_SERIAL_8250_8250_H

#include "linux/serial_core.h"
#include "linux/console.h"
#include "linux/tty.h"

extern struct uart_port uart8250_port0;
extern struct uart_driver serial8250_uart_driver;
extern struct tty_driver tty_serial8250_driver;

void serial_put_char(char c);
void init_serial(void);

#endif
