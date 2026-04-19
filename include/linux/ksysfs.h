#ifndef LINUX_KSYSFS_H
#define LINUX_KSYSFS_H

#include "linux/kobject.h"

/* Linux-like /sys/kernel root kobject. */
extern struct kobject *kernel_kobj;

#endif
