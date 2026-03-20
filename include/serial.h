#ifndef SERIAL_H
#define SERIAL_H

#include "isr.h"

/* Serial Helper Functions */
void serial_init(void);
void serial_put_str(const char* data);
void serial_put_char(char a);
void serial_handler(struct registers* regs);

#endif
