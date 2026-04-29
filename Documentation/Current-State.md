# Lite Current State

## Purpose

This document is the current high-level analysis of the Lite codebase. It
describes what the tree implements today, how the major subsystems fit together,
and where the important Linux 2.6 gaps remain.

## Project Goal

Lite is a runnable 32-bit x86 teaching kernel that is being progressively aligned
with Linux 2.6 vocabulary, placement, and subsystem boundaries.

The goal is not to clone Linux wholesale. The goal is to keep a small, runnable
kernel while making each subsystem use Linux-shaped concepts and file ownership:
same concept, same name, same rough location, with explicit documentation for
intentional differences.

## Current System Summary

Lite currently has a real operating-system spine:

- 32-bit x86 Multiboot entry, high-half kernel mapping, GDT/TSS, IDT, traps,
  system calls, legacy PIC IRQs, and APIC/IOAPIC placeholders.
- Boot memory parsing, zones, buddy allocator, paging, vmalloc, slab, mmap/brk,
  page faults, COW, rmap, vmscan, swap, and page cache.
- Kernel tasks, fork/exec/exit/wait, PID allocation, credentials, signal basics,
  kthreads, timer tick scheduling, and a simple scheduler.
- VFS, dcache/inode/file abstractions, mount overlay semantics, initramfs,
  ramfs, procfs, sysfs, devtmpfs, minix, block device files, char device files,
  buffer cache, and generic file I/O through page cache.
- Driver core with device/driver/bus/class/kobject/kset/kref, synchronous probe,
  uevent formatting, sysfs projection, and devtmpfs node creation.
- PCI enumeration, PCI driver matching, PCIe capability detection, virtio-pci,
  virtqueue helpers, virtio-scsi, SCSI host/target/device/disk, NVMe PCI
  controller setup, namespace discovery, and gendisk registration.
- User-space init, shell, runtime library, and smoke test payload packed into
  initramfs.

The system is best understood as a small Linux-shaped kernel with runnable
integration paths, not as a complete Linux-compatible kernel.

## Boot and Init

Boot starts in `arch/x86/boot/header.S`, enters the high-half kernel, and reaches
`start_kernel()` in `init/main.c`.

Current boot sequence:

1. Initialize serial output and architecture setup.
2. Parse Multiboot memory and initrd/module information.
3. Bring up bootmem, zones, buddy allocation, paging, mem init, vmscan, swap,
   slab, scheduler, and fork state.
4. Install timer and run early initcalls.
5. `rest_init()` creates `kernel_init` as PID 1 and leaves PID 0 in the idle
   loop.
6. `kernel_init()` runs `driver_init()`, normal initcalls, syscall init, VFS,
   initramfs unpacking, sysfs/devtmpfs/proc mounts, block-backed minix mounts,
   and finally execs init.

Current status:

- Initcall levels exist and are used.
- The ordering is Linux-shaped but still much simpler than Linux 2.6.
- Error handling during early boot is mostly panic-based.

Main gaps:

- Initcall level semantics and exact ordering still need a more disciplined
  Linux 2.6 pass.
- Boot parameter parsing is minimal.
- SMP boot is not implemented.

## Architecture and Interrupts

The architecture layer is currently UP x86 with legacy PIC as the real interrupt
controller.

Current status:

- IDT/trap/syscall/IRQ entry exists.
- Legacy PIC IRQ0-IRQ15 path works.
- APIC, IOAPIC, and IPI vector boundaries exist as placeholders.
- TSS is used for kernel stack switching when entering from user mode.

Main gaps:

- No real LAPIC timer.
- No IOAPIC routing.
- No SMP startup or IPI send path.
- IRQ core lacks Linux's full flow handlers, descriptor state, vector
  management, and per-CPU accounting.

These gaps should stay explicit until synchronization primitives, task lifetime,
and per-CPU foundations are stronger.

## Memory Management

The memory subsystem is a minimal but real stack.

Current status:

- Multiboot memory map is parsed into boot memory metadata.
- Zones and a buddy-style page allocator exist.
- Kernel high-half mapping, direct map helpers, vmalloc, and user page tables
  exist.
- `mmap`, `brk`, VMA lookup, page fault allocation, COW, and rmap are present.
- Slab provides `kmalloc`/`kfree`.
- Page cache and writeback hooks exist.
- Vmscan and swap form a minimal reclaim path.

Main gaps:

- No highmem/PAE/NUMA.
- No per-cpu page lists.
- Reclaim and LRU behavior are very small compared with Linux 2.6.
- File-backed demand paging is not complete.
- Writeback lacks Linux 2.6's dirty throttling, BDI model, pdflush/kupdate
  style background behavior, and congestion feedback.
- IDR/radix-tree support exists but remains simplified.

## Process and Scheduler

Current status:

- Kernel threads and user tasks are represented by `task_struct`.
- `fork`, `exec`, `exit`, `wait`, signal basics, credentials, PID allocation,
  and timer-driven scheduling exist.
- COW makes forked address spaces usable.

Main gaps:

- Scheduler is still a small UP scheduler, not Linux 2.6 O(1) scheduler
  semantics.
- No per-CPU runqueues or real SMP safety.
- Task reference counting and tasklist locking are simplified.
- Waitqueue locking and signal delivery remain partial.

## VFS and Filesystems

Current status:

- VFS uses inodes, dentries, files, file operations, and address spaces.
- Mount overlay behavior exists.
- Initramfs unpacks `cpio newc` into root ramfs.
- procfs/sysfs/devtmpfs provide observability and device nodes.
- Minix supports block-backed file operations used by smoke tests.
- Buffer cache bridges filesystem block access to block devices.
- Generic file I/O uses page cache.

Main gaps:

- VFS object lifetimes and reference semantics are simpler than Linux.
- Mount namespace behavior is minimal.
- No ext2 or mature on-disk filesystem layer.
- Buffer/page/writeback integration still needs a deeper Linux 2.6 pass.
- Error handling is often panic-based in mount paths.

## Device Model

Current status:

- `kobject`, `kset`, `kref`, `device`, `driver`, `bus_type`, `class`, and
  `device_type` exist.
- Buses/classes project into sysfs.
- Device registration attempts synchronous driver binding.
- Devtmpfs creates char and block device nodes from registered devices.
- Uevent formatting exists as a minimal kernel-side event stream.

Main gaps:

- Reference counting and object release behavior are simplified.
- Probe/remove concurrency is not Linux-like.
- Deferred probe/debug paths are still visible in current code.
- Sysfs lifetime, symlink, attribute ownership, and uevent behavior are
  partial.

## Block and Storage

Current block I/O path:

`block_device_read/write -> bio -> submit_bio -> generic_make_request -> request_queue -> request_fn -> driver completion`

Current status:

- `gendisk`, `block_device`, `request_queue`, `bio`, and `request` exist.
- `gendisk` owns the queue, matching the important Linux ownership direction.
- `add_disk()` exposes block devices through driver core, sysfs, and devtmpfs.
- Ramdisk, SCSI disk, and NVMe namespace all register disks.
- SCSI/virtio-scsi and NVMe can participate in smoke paths.

Main gaps:

- No blk-mq, no elevator framework, no real I/O scheduler.
- Request merging and plugging are minimal or absent.
- No partition model beyond whole-disk bdev.
- No robust timeout/reset/error recovery in block core.
- Queue accounting and sysfs are intentionally small.
- Several current paths still need cleanup around request completion, bdev
  lifetime, overflow-safe bounds checks, and debug instrumentation.

## PCI, Virtio, SCSI, and NVMe

Current status:

- PCI scans BDFs, reads config space, creates `pci_dev`, and matches
  `pci_driver` ID tables.
- PCIe capability detection exists and NVMe requires PCIe capability.
- Virtio-pci transport creates virtio devices and virtqueues.
- Virtio-scsi registers as a virtio driver, creates a SCSI host, and scans a
  deliberately conservative target/LUN window.
- SCSI midlayer has host, target, device, disk, TUR/INQUIRY/READ CAPACITY,
  READ_10, WRITE_10, and `sd` disk registration.
- NVMe PCI driver maps BAR0, configures admin and I/O queues, identifies one
  active namespace, creates a gendisk, and handles synchronous read/write I/O.
- NVMe controller ready waits use Linux's `(CAP.TO + 1) * 500ms` timeout
  interpretation.
- NVMe Set Features queue-count status follows Linux's local `set_queue_count`
  handling: a controller status maps to zero queues, while transport failure
  remains an error.
- NVMe I/O queue setup stops when queue-count negotiation returns no usable
  queues, matching Linux setup flow.
- NVMe controller disable preserves `ctrl_config` and clears SHN/EN bits like
  Linux `nvme_disable_ctrl()`.
- NVMe host code no longer carries the temporary `TRAEDBG` smoke-debug helper;
  remaining storage debug output is outside the NVMe host driver.
- The old `scsi-nvme-smoke` mount/minix debug prints have been removed from
  the active storage smoke path.
- The old `/mnt_nvme`-specific VFS open `TRAEDBG` watch path has been removed.
- The old `nvme_rw.txt`-specific Minix create `TRAEDBG` watch path has been
  removed.

Main gaps:

- PCI has no full bridge/resource/ECAM/MSI/MSI-X model.
- Virtio is a small transport implementation, not full feature negotiation and
  modern virtio coverage.
- SCSI has no full async scan, EH, task management, sense handling, or hotplug.
- NVMe uses polling, one I/O queue, synchronous commands, limited PRP handling,
  and no interrupt/MSI-X completion path.

## Console, TTY, and Input

Current status:

- Serial output is the main console path.
- TTY core and `n_tty` exist in minimal form.
- 8250 serial and i8042/AT keyboard paths exist.
- User shell interaction is routed through the minimal tty path.

Main gaps:

- No full Linux console list semantics.
- No VT, pty, session/pgrp terminal semantics, or mature line discipline model.
- `printk` remains much simpler than Linux 2.6.

## Testing and Observability

Current status:

- `make -j4` builds kernel and initramfs payloads.
- `make smoke-128` and `make smoke-512` run QEMU smoke tests.
- `/proc`, `/sys`, and devtmpfs expose minimal runtime state.
- There are still `TRAEDBG` traces left in device-core observability paths.

Main gaps:

- No unit-test harness for individual kernel subsystems.
- Smoke tests are the main regression signal.
- Debug instrumentation needs a cleanup pass after the current NVMe/storage
  stabilization stage.

## Current Priority

The next work should not broaden scope. The project is far enough along that
the main risk is uncontrolled cross-subsystem churn.

The correct near-term posture is:

1. Stabilize and clean up the current storage/device path.
2. Remove stale debug instrumentation once evidence is no longer needed.
3. Revalidate `make -j4`, `make smoke-128`, and `make smoke-512`.
4. Then resume staged Linux 2.6 alignment in dependency order.
