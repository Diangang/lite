#include <stdint.h>

#include "linux/device.h"
#include "linux/kobject.h"

/*
 * Linux mapping: linux2.6/lib/kobject_uevent.c::uevent_seqnum
 *
 * Lite does not implement full netlink/usermode helper delivery. It keeps only
 * the global sequence counter that backs /sys/kernel/uevent_seqnum and the
 * in-kernel text buffer exported from driver core.
 */

char uevent_helper[UEVENT_HELPER_PATH_LEN];
uint32_t uevent_seqnum;
