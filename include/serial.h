#ifndef SERIAL_H
#define SERIAL_H

#include "isr.h"

/* Serial Helper Functions */
void init_serial(void);
void serial_put_char(char a);
void serial_handler();

#endif
