# Lite Source Tree

## Purpose

This is the current directory map for the Lite kernel tree. For behavior and
progress, read `Current-State.md`; for future work, read `Linux26-Roadmap.md`.

## Top Level

- `Makefile`
  - Builds the 32-bit x86 kernel, initramfs user programs, QEMU run targets,
    and smoke targets.

- `arch/x86/`
  - x86 boot, entry, traps, IRQ, PIC/APIC placeholders, setup, and linker
    layout.
  - Key files: `boot/header.S`, `entry/entry_32.S`, `entry/syscall_32.c`,
    `kernel/head32.c`, `kernel/setup.c`, `kernel/traps.c`,
    `kernel/irq.c`, `kernel/i8259.c`, `kernel/apic/*`.

- `init/`
  - Kernel initialization and initramfs unpacking.
  - Key files: `main.c`, `initramfs.c`, `version.c`.

- `kernel/`
  - Scheduler, process lifecycle, PID, credentials, signal, kthreads, panic,
    printk, sysfs kernel attributes, and time.
  - Key files: `sched/core.c`, `sched/wait.c`, `fork.c`, `exit.c`,
    `signal.c`, `kthread.c`, `printk/printk.c`, `time/*`.

- `mm/`
  - Boot memory, zones, page allocator, paging, mmap/brk, page faults, vmalloc,
    slab, filemap, writeback, vmscan, rmap, and swap.
  - Key files: `bootmem.c`, `mmzone.c`, `page_alloc.c`, `memory.c`,
    `mmap.c`, `vmalloc.c`, `slab.c`, `filemap.c`, `page-writeback.c`,
    `vmscan.c`, `rmap.c`, `swap.c`.

- `fs/`
  - VFS, file descriptors, path lookup, namespace/mounts, block device files,
    char device files, buffer cache, procfs, sysfs, ramfs, and minix.
  - Key files: `namespace.c`, `namei.c`, `open.c`, `read_write.c`,
    `file.c`, `exec.c`, `block_dev.c`, `buffer.c`, `proc/*`, `sysfs/*`,
    `ramfs/inode.c`, `minix/*`.

- `block/`
  - Minimal Linux-shaped block core.
  - Key files: `blk-core.c`, `blk-sysfs.c`, `genhd.c`.

- `drivers/base/`
  - Minimal driver core, device model, bus/class/driver binding, platform
    devices, devtmpfs, and virtual devices.

- `drivers/pci/`
  - PCI config access, device enumeration, driver matching, sysfs exposure,
    and PCIe capability detection.

- `drivers/virtio/`
  - Minimal virtio bus, virtio-pci transport, legacy/modern split, and
    virtqueue helpers.

- `drivers/scsi/`
  - Minimal SCSI host/device/target/disk model plus virtio-scsi frontend.

- `drivers/nvme/host/`
  - Minimal PCI NVMe controller and namespace driver.

- `drivers/block/`
  - Memory-backed block devices.

- `drivers/tty/`
  - TTY core, line discipline, serial core, and 8250 serial support.

- `drivers/input/`
  - i8042, serio, and AT keyboard path.

- `drivers/clocksource/`
  - PIT/i8253 clock source support.

- `include/`
  - Public kernel headers.
  - `include/linux/` holds Linux-shaped subsystem headers.
  - `include/scsi/` holds SCSI headers.
  - `arch/x86/include/asm/` holds x86-specific headers.

- `lib/`
  - Shared kernel helpers and data structures: string/vsprintf, kobject,
    klist, idr, radix tree, rbtree, bitmap, parser.

- `usr/`
  - User-space initramfs payload: init, shell, smoke test, runtime library,
    and user linker script.

- `Documentation/`
  - Current docs and archived historical docs.

## Runtime Shape

The intended current boot path is:

`arch/x86 boot -> start_kernel -> rest_init -> kernel_init -> driver_init/initcalls -> VFS/devtmpfs/sysfs/minix mounts -> /sbin/init`

The intended current storage path is:

`VFS/minix/buffer -> fs/block_dev.c -> submit_bio -> block request_queue -> brd/scsi/nvme request_fn`
