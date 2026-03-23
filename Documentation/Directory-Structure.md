# Lite OS Directory Structure vs Linux 2.6

This document outlines the current directory structure of Lite OS and how it meticulously maps to the canonical Linux 2.6 kernel source tree.

## Top-Level Directories

### `arch/`
Architecture-specific code. Just like Linux, we separate generic kernel logic from hardware-bound implementations.
- **`arch/x86/boot/`**: Contains `boot.s`, the extremely early Multiboot compliant assembly entry point. This matches Linux's `arch/x86/boot/` setup.
- **`arch/x86/kernel/`**: Contains the core architecture-specific mechanisms like GDT (`gdt.c`), IDT (`idt.c`), ISRs (`isr.c`, `interrupt.s`), and the linker script (`linker.ld`).

### `init/`
Kernel initialization and early setup.
- **`init/main.c`**: The C-level entry point (`kernel_main()`, analogous to Linux's `start_kernel()`). It orchestrates the initialization of all subsystems.
- **`init/initramfs.c`**: Contains the logic to parse and extract the `cpio` archive into the root `ramfs` during early boot, exactly as Linux does.

### `kernel/`
Core, architecture-independent kernel mechanisms.
- Contains the scheduler and process management (`task.c`), the system call multiplexer (`syscall.c`), and built-in debugging tools (`shell.c`).

### `mm/`
Memory Management subsystem.
- Contains Physical Memory Management (`pmm.c`), Virtual Memory Management/Paging (`vmm.c`), Kernel Heap (`kheap.c`), and the newly implemented Page Cache (`filemap.c`).

### `fs/`
The Virtual File System (VFS) and concrete filesystem implementations.
- Contains generic VFS components: `inode.c`, `dentry.c`, `namei.c` (path resolution), `file.c`, `open.c`, `read_write.c`, `namespace.c` (mounts).
- Subdirectories contain specific filesystems: `ramfs/`, `procfs/`, `sysfs/`, `devfs/`.

### `drivers/`
Device drivers categorized by subsystem, mirroring the Linux `drivers/` tree.
- **`drivers/base/`**: The generic Device Model (kobjects, bus, drivers, devices).
- **`drivers/video/`**: Display drivers (e.g., `vga.c`).
- **`drivers/clocksource/`**: System timers (e.g., `timer.c`).
- **`drivers/tty/`**: Teletype and serial drivers (`tty.c`, `serial.c`).
- **`drivers/input/`**: Input devices (`keyboard.c`).
- **`drivers/console/`**: High-level console routing.

### `include/`
Header files.
*(Note: To keep include paths simple in this micro-macrokernel, we keep headers flat here rather than strictly dividing into `include/linux/` and `include/asm/`, though logically they map to those boundaries).*

### `lib/`
Generic library functions (e.g., `libc.c` for `string.h` implementations) used internally by the kernel.

### `usr/`
Early user-space programs and the initramfs payload generator.
- Contains the C/assembly source for `init.elf`, `shell.elf` (user shell), `smoke.elf` (automated test suite), etc.
- Also contains the user-space runtime library (`ulib.c`, `ulib.h`) and entry point (`crt0.s`).
- During the build process, these are compiled, packed into `initramfs.cpio`, and fed to the kernel, perfectly mirroring the concept of Linux's `usr/gen_init_cpio` and default early user-space.

### `out/`
Build output directory.
- All compiled artifacts (`.o`, `.elf`, `.bin`, `.iso`, `.cpio`) are generated here to keep the source tree clean. This directory is automatically removed during `make clean`.

### `Documentation/`
(Formerly `docs/`) Contains all architectural design documents, issue trackers, and Q&A logs.
