#include "linux/device.h"
#include "linux/libc.h"
#include "linux/pci.h"
#include "linux/virtio.h"

static char uevent_buf[4096];
static uint32_t uevent_len = 0;
static uint32_t uevent_seqnum = 0;

static int buf_append(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    if (!buf || !off || !s)
        return 0;
    uint32_t n = (uint32_t)strlen(s);
    if (*off + n >= cap)
        return 0;
    memcpy(buf + *off, s, n);
    *off += n;
    buf[*off] = 0;
    return 1;
}

static int buf_append_ch(char *buf, uint32_t *off, uint32_t cap, char c)
{
    if (!buf || !off)
        return 0;
    if (*off + 1 >= cap)
        return 0;
    buf[(*off)++] = c;
    buf[*off] = 0;
    return 1;
}

static int buf_append_u32_dec(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[16];
    itoa((int)v, 10, tmp);
    return buf_append(buf, off, cap, tmp);
}

static int buf_append_u32_oct(char *buf, uint32_t *off, uint32_t cap, uint32_t value)
{
    char tmp[16];
    uint32_t pos = 0;

    if (value == 0)
        tmp[pos++] = '0';
    else {
        char rev[16];
        uint32_t rev_pos = 0;
        while (value && rev_pos < sizeof(rev)) {
            rev[rev_pos++] = (char)('0' + (value & 7u));
            value >>= 3;
        }
        while (rev_pos)
            tmp[pos++] = rev[--rev_pos];
    }
    tmp[pos] = 0;
    return buf_append(buf, off, cap, tmp);
}

static void u32_to_hex_fixed(uint32_t v, char *out, uint32_t width)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < width; i++) {
        uint32_t shift = (width - 1 - i) * 4;
        out[i] = hex[(v >> shift) & 0xF];
    }
    out[width] = 0;
}

static int kobject_build_path(struct kobject *kobj, const char *prefix, char *buf, uint32_t cap)
{
    if (!kobj || !prefix || !buf || cap == 0)
        return -1;
    const char *stack[16];
    uint32_t depth = 0;
    struct kobject *cur = kobj;
    while (cur && depth < (uint32_t)(sizeof(stack) / sizeof(stack[0]))) {
        stack[depth++] = cur->name;
        cur = cur->parent;
    }
    uint32_t off = 0;
    uint32_t pre = (uint32_t)strlen(prefix);
    if (pre + 1 >= cap)
        return -1;
    memcpy(buf + off, prefix, pre);
    off += pre;
    for (int i = (int)depth - 1; i >= 0; i--) {
        const char *name = stack[i];
        if (!name || !*name)
            continue;
        uint32_t n = (uint32_t)strlen(name);
        if (off + 1 + n + 1 >= cap)
            return -1;
        buf[off++] = '/';
        memcpy(buf + off, name, n);
        off += n;
    }
    buf[off] = 0;
    return 0;
}

int device_get_devpath(struct device *dev, char *buf, uint32_t cap)
{
    if (!dev)
        return -1;
    return kobject_build_path(&dev->kobj, "/devices", buf, cap);
}

int device_get_sysfs_path(struct device *dev, char *buf, uint32_t cap)
{
    if (!dev || !buf || cap == 0)
        return -1;
    char devpath[256];
    if (device_get_devpath(dev, devpath, sizeof(devpath)) != 0)
        return -1;
    uint32_t pre = 4;
    uint32_t dlen = (uint32_t)strlen(devpath);
    if (pre + dlen + 1 > cap)
        return -1;
    memcpy(buf, "/sys", pre);
    memcpy(buf + pre, devpath, dlen + 1);
    return 0;
}

int device_get_modalias(struct device *dev, char *buf, uint32_t cap)
{
    if (!dev || !buf || cap == 0)
        return -1;
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "pci")) {
        struct pci_dev *pdev = pci_get_pci_dev(dev);
        if (!pdev)
            return -1;
        uint32_t off = 0;
        char hx[8];
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "pci:v"))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->vendor, hx, 4);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append_ch(buf, &off, cap, 'd'))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->device, hx, 4);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append(buf, &off, cap, "bc"))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->class, hx, 2);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append(buf, &off, cap, "sc"))
            return -1;
        u32_to_hex_fixed((uint32_t)pdev->subclass, hx, 2);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        return 0;
    }
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "platform")) {
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "platform:"))
            return -1;
        if (!buf_append(buf, &off, cap, dev->kobj.name))
            return -1;
        return 0;
    }
    if (dev->bus && !strcmp(dev->bus->subsys.kset.kobj.name, "virtio")) {
        /*
         * Linux mapping: drivers/virtio/virtio.c exports modalias as:
         *   "virtio:d%08Xv%08X"
         */
        struct virtio_device *vdev = dev_to_virtio(dev);
        if (!vdev)
            return -1;
        uint32_t off = 0;
        char hx[16];
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, "virtio:d"))
            return -1;
        u32_to_hex_fixed(vdev->id.device, hx, 8);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        if (!buf_append_ch(buf, &off, cap, 'v'))
            return -1;
        u32_to_hex_fixed(vdev->id.vendor, hx, 8);
        if (!buf_append(buf, &off, cap, hx))
            return -1;
        return 0;
    }
    if (dev->bus) {
        uint32_t off = 0;
        buf[0] = 0;
        if (!buf_append(buf, &off, cap, dev->bus->subsys.kset.kobj.name))
            return -1;
        if (!buf_append_ch(buf, &off, cap, ':'))
            return -1;
        if (!buf_append(buf, &off, cap, dev->kobj.name))
            return -1;
        return 0;
    }
    uint32_t off = 0;
    buf[0] = 0;
    if (!buf_append(buf, &off, cap, "device:"))
        return -1;
    if (!buf_append(buf, &off, cap, dev->kobj.name))
        return -1;
    return 0;
}

void device_uevent_emit(const char *action, struct device *dev)
{
    if (!action || !dev || !dev->kobj.name[0])
        return;

    char tmp[512];
    char devpath[256];
    char modalias[128];
    const char *devnode;
    uint32_t mode = 0;
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t off = 0;
    tmp[0] = 0;
    devpath[0] = 0;
    modalias[0] = 0;

    device_get_devpath(dev, devpath, sizeof(devpath));
    device_get_modalias(dev, modalias, sizeof(modalias));

    if (!buf_append(tmp, &off, sizeof(tmp), "ACTION=") ||
        !buf_append(tmp, &off, sizeof(tmp), action) ||
        !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append(tmp, &off, sizeof(tmp), "DEVPATH=") ||
        !buf_append(tmp, &off, sizeof(tmp), devpath[0] ? devpath : "/devices") ||
        !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append(tmp, &off, sizeof(tmp), "SUBSYSTEM="))
        return;
    if (dev->class) {
        if (!buf_append(tmp, &off, sizeof(tmp), dev->class->subsys.kset.kobj.name))
            return;
    } else if (dev->bus) {
        if (!buf_append(tmp, &off, sizeof(tmp), dev->bus->subsys.kset.kobj.name))
            return;
    } else {
        if (!buf_append(tmp, &off, sizeof(tmp), "unknown"))
            return;
    }
    if (!buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (modalias[0]) {
        if (!buf_append(tmp, &off, sizeof(tmp), "MODALIAS=") ||
            !buf_append(tmp, &off, sizeof(tmp), modalias) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    devnode = device_get_devnode(dev, &mode, &uid, &gid);
    if (devnode && devnode[0]) {
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVNAME=") ||
            !buf_append(tmp, &off, sizeof(tmp), devnode) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVMODE=") ||
            !buf_append_u32_oct(tmp, &off, sizeof(tmp), mode & 0777) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVUID=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), uid) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "DEVGID=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), gid) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (dev->devt) {
        if (!buf_append(tmp, &off, sizeof(tmp), "MAJOR=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), MAJOR(dev->devt)) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
        if (!buf_append(tmp, &off, sizeof(tmp), "MINOR=") ||
            !buf_append_u32_dec(tmp, &off, sizeof(tmp), MINOR(dev->devt)) ||
            !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
            return;
    }
    if (!buf_append(tmp, &off, sizeof(tmp), "SEQNUM=") ||
        !buf_append_u32_dec(tmp, &off, sizeof(tmp), ++uevent_seqnum) ||
        !buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;
    if (!buf_append_ch(tmp, &off, sizeof(tmp), '\n'))
        return;

    if (off >= sizeof(uevent_buf) || off >= sizeof(tmp))
        return;
    if (off + uevent_len > sizeof(uevent_buf)) {
        uint32_t need = (off + uevent_len) - (uint32_t)sizeof(uevent_buf);
        if (need >= uevent_len) {
            uevent_len = 0;
        } else {
            uint32_t new_len = uevent_len - need;
            for (uint32_t i = 0; i < new_len; i++)
                uevent_buf[i] = uevent_buf[need + i];
            uevent_len = new_len;
        }
    }
    memcpy(uevent_buf + uevent_len, tmp, off);
    uevent_len += off;
}

uint32_t device_uevent_read(uint32_t offset, uint32_t size, uint8_t *buffer)
{
    if (!buffer || offset >= uevent_len)
        return 0;
    uint32_t remain = uevent_len - offset;
    if (size > remain)
        size = remain;
    memcpy(buffer, uevent_buf + offset, size);
    return size;
}

/*
 * Linux mapping: uevent sequence number is a global monotonic counter used by
 * the uevent layer. Lite exposes it for sysfs (/sys/kernel/uevent_seqnum).
 */
uint32_t device_uevent_seqnum(void)
{
    return uevent_seqnum;
}
