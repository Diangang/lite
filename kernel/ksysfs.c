#include "linux/ksysfs.h"
#include "linux/sysfs.h"

/* ksysfs_init: Initialize ksysfs. */
void ksysfs_init(void)
{
    init_sysfs();
}
