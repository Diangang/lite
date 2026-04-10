#ifndef LINUX_KSYSFS_H
#define LINUX_KSYSFS_H

#include "linux/kobject.h"

/* /sys/kernel anchor object, populated by kernel/ksysfs.c */
extern struct kobject kernel_kobj;

#endif
