#ifndef LINUX_PANIC_H
#define LINUX_PANIC_H

__attribute__((noreturn))
void panic(const char *msg);

#endif
