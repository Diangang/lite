# Linux 2.6 Placement DIFF (38 files) - Fix Plan

> Auto-generated from `Documentation/linux-alignment-ledger/files.placement.diff.json`.
> Rule: every Lite kernel file must end up aligned to `linux2.6/<path>`: either `git mv` to the exact Linux path, or split/merge into Linux files and delete the Lite-only file name.

## Usage
- Mark progress by changing `Status: [ ]` to `Status: [x]` for each item.
- After each item: run `make -j4` and at least `make smoke-128`.

### `arch/x86/boot/boot.s`
- Status: [x]
- Diff: `linux2.6/arch/x86/boot/boot.s` does not exist (path mismatch)
- Linux Target(s): `arch/x86/boot/header.S`
- Plan: 直接改名到 `header.S`（Linux 对应落位），保持当前 Multiboot 子集实现不变；更新构建入口并删除旧文件名。
- Verify: `make -j4` + `make smoke-128`

### `arch/x86/kernel/gdt.c`
- Status: [x]
- Diff: `linux2.6/arch/x86/kernel/gdt.c` does not exist (path mismatch)
- Linux Target(s): `arch/x86/kernel/cpu/common.c`
- Plan: 按 linux2.6 职责将 GDT/TSS 初始化实现并入 `arch/x86/kernel/cpu/common.c`，删除 `gdt.c`。
- Verify: `make -j4` + `make smoke-128`

### `arch/x86/kernel/idt.c`
- Status: [x]
- Diff: `linux2.6/arch/x86/kernel/idt.c` does not exist (path mismatch)
- Linux Target(s): `arch/x86/kernel/traps.c`
- Plan: 按 linux2.6 职责将 IDT 结构与初始化并入 `arch/x86/kernel/traps.c`，删除 `idt.c`。
- Verify: `make -j4` + `make smoke-128`

### `arch/x86/kernel/interrupt.s`
- Status: [x]
- Diff: `linux2.6/arch/x86/kernel/interrupt.s` does not exist (path mismatch)
- Linux Target(s): `arch/x86/entry/entry_32.S`
- Plan: git mv 到 linux2.6 同名落位；之后按 Linux 分层把 C 侧 handler glue 收敛到 arch/x86/kernel/{irq*,traps}.c。
- Verify: `make -j4` + `make smoke-128`

### `arch/x86/kernel/isr.c`
- Status: [x]
- Diff: `linux2.6/arch/x86/kernel/isr.c` does not exist (path mismatch)
- Linux Target(s): `arch/x86/kernel/traps.c`, `arch/x86/kernel/irq.c`
- Plan: 按 linux2.6 职责将异常/IDT 侧逻辑并入 `traps.c`，将 IRQ 安装与 dispatch 并入 `irq.c`，删除 `isr.c`。
- Verify: `make -j4` + `make smoke-128`

### `block/blkdev.c`
- Status: [x]
- Diff: `linux2.6/block/blkdev.c` does not exist (path mismatch)
- Linux Target(s): `block/genhd.c`, `fs/block_dev.c`, `block/blk-sysfs.c`
- Plan: 将 gendisk/块设备 class+sysfs(disk/queue) 收敛到 `block/genhd.c`/`block/blk-sysfs.c`，将 bdev registry 与 block_device_read/write/accounting 收敛到 `fs/block_dev.c`，删除旧 `blkdev.c` 并更新构建入口。
- Verify: `make -j4` + `make smoke-128`

### `drivers/base/uevent.c`
- Status: [x]
- Diff: `linux2.6/drivers/base/uevent.c` does not exist (path mismatch)
- Linux Target(s): `drivers/base/core.c`
- Plan: linux2.6 该树里无 drivers/base/uevent.c：把 Lite 的 uevent 缓冲/DEVPATH/MODALIAS 生成与 emit/read/seqnum 全部并入 drivers/base/core.c，删除旧文件并批量更新构建入口。
- Verify: `make -j4` + `make smoke-128`

### `drivers/base/virtual.c`
- Status: [x]
- Diff: `linux2.6/drivers/base/virtual.c` does not exist (path mismatch)
- Linux Target(s): `drivers/base/core.c`, `drivers/base/class.c`
- Plan: basename 命中 linux2.6 的 drivers/regulator/virtual.c 是误匹配：禁止直接 mv。先按符号/职责把 virtual device/class 逻辑并入 drivers/base/{core,class}.c，原文件删除。
- Verify: `make -j4` + `make smoke-128`

### `drivers/block/ramdisk.c`
- Status: [x]
- Diff: `linux2.6/drivers/block/ramdisk.c` does not exist (path mismatch)
- Linux Target(s): `drivers/block/brd.c`
- Plan: git mv drivers/block/ramdisk.c drivers/block/brd.c；对齐 brd 命名/符号（保持现有功能子集）。
- Verify: `make -j4` + `make smoke-128`

### `drivers/clocksource/timer.c`
- Status: [x]
- Diff: `linux2.6/drivers/clocksource/timer.c` does not exist (path mismatch)
- Linux Target(s): `drivers/clocksource/i8253.c`, `arch/x86/kernel/i8253.c`
- Plan: 先判定 Lite timer.c 是否为 x86 PIT：是则落位到 drivers/clocksource/i8253.c，否则按实现职责落位到 arch/x86/kernel/i8253.c 或对应 clocksource 驱动文件；禁止按 basename 迁到 net/sound 的 timer.c。
- Verify: `make -j4` + `make smoke-128`

### `drivers/pci/pcie/pcie.c`
- Status: [x]
- Diff: `linux2.6/drivers/pci/pcie/pcie.c` does not exist (path mismatch)
- Linux Target(s): `include/linux/pci.h`
- Plan: Lite `pcie.c` 仅提供 PCIe capability helper；按 linux2.6 的落位把实现收敛到 `include/linux/pci.h`（static inline），删除旧文件并移除构建入口。
- Verify: `make -j4` + `make smoke-128`

### `fs/chrdev.c`
- Status: [x]
- Diff: `linux2.6/fs/chrdev.c` does not exist (path mismatch)
- Linux Target(s): `fs/char_dev.c`
- Plan: git mv fs/chrdev.c fs/char_dev.c；修 include 与 Makefile 引用。
- Verify: `make -j4` + `make smoke-128`

### `fs/dentry.c`
- Status: [x]
- Diff: `linux2.6/fs/dentry.c` does not exist (path mismatch)
- Linux Target(s): `fs/dcache.c`
- Plan: git mv fs/dentry.c fs/dcache.c；修 include 与 Makefile 引用。
- Verify: `make -j4` + `make smoke-128`

### `fs/fdtable.c`
- Status: [x]
- Diff: `linux2.6/fs/fdtable.c` does not exist (path mismatch)
- Linux Target(s): `fs/file.c`, `include/linux/fdtable.h`
- Plan: linux2.6 通常无独立 fs/fdtable.c：把实现并入 fs/file.c（fdtable 管理属于 fs/file.c），删除原文件并更新构建入口。
- Verify: `make -j4` + `make smoke-128`

### `include/asm/gdt.h`
- Status: [x]
- Diff: `linux2.6/include/asm/gdt.h` does not exist (path mismatch)
- Linux Target(s): `arch/x86/include/asm/desc.h`
- Plan: 把 GDT/TSS 相关声明并入 arch/x86/include/asm/desc.h，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/asm/idt.h`
- Status: [x]
- Diff: `linux2.6/include/asm/idt.h` does not exist (path mismatch)
- Linux Target(s): `arch/x86/include/asm/desc.h`
- Plan: 把 IDT 相关声明并入 arch/x86/include/asm/desc.h，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/asm/multiboot.h`
- Status: [x]
- Diff: `linux2.6/include/asm/multiboot.h` does not exist (path mismatch)
- Linux Target(s): `arch/x86/include/asm/setup.h`
- Plan: 把 multiboot 结构与内存类型常量并入 arch/x86/include/asm/setup.h，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/blk_queue.h`
- Status: [x]
- Diff: `linux2.6/include/linux/blk_queue.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/blkdev.h`
- Plan: Lite 当前 `blk_queue.h` 仅为 `blkdev.h` 包装头；直接把所有引用收敛到 `blkdev.h`，删除旧头。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/blk_request.h`
- Status: [x]
- Diff: `linux2.6/include/linux/blk_request.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/blkdev.h`
- Plan: Lite 当前 `blk_request.h` 仅为 `blkdev.h` 包装头；直接把所有引用收敛到 `blkdev.h`，删除旧头。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/chrdev.h`
- Status: [x]
- Diff: `linux2.6/include/linux/chrdev.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/cdev.h`
- Plan: 对齐为 cdev.h：把 chrdev.h 内容迁入 cdev.h（或直接改名），删除 chrdev.h，并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/clockevents.h`
- Status: [x]
- Diff: `linux2.6/include/linux/clockevents.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/clockchips.h`
- Plan: Lite 当前 `clockevents.h` 的内容全部属于 clockevent/device 声明；整体迁移到 `clockchips.h`，批量改 include 后删除 `clockevents.h`。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/devtmpfs.h`
- Status: [x]
- Diff: `linux2.6/include/linux/devtmpfs.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/device.h`
- Plan: linux2.6 该树里 devtmpfs_mount 声明在 include/linux/device.h：迁移声明/宏到 device.h，删除 devtmpfs.h。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/exit.h`
- Status: [x]
- Diff: `linux2.6/include/linux/exit.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/sched.h`
- Plan: 将对外声明合并到 include/linux/sched.h（该树存在），删除 exit.h 并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/fork.h`
- Status: [x]
- Diff: `linux2.6/include/linux/fork.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/sched.h`
- Plan: 将对外声明合并到 include/linux/sched.h，删除 fork.h 并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/ksysfs.h`
- Status: [x]
- Diff: `linux2.6/include/linux/ksysfs.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/sysfs.h`, `include/linux/kobject.h`
- Plan: linux2.6 该树无 ksysfs.h：把对外 API 合并到 sysfs.h 或 kobject.h（按调用点裁决），删除 ksysfs.h 并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/libc.h`
- Status: [x]
- Diff: `linux2.6/include/linux/libc.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/io.h, include/linux/string.h, include/linux/printk.h, include/linux/kernel.h`
- Plan: 把端口 IO 拆到 io.h，把字符串/复制原型收敛到 string.h，把 printf/linux_banner 收敛到 printk.h，删除 libc.h 并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/memlayout.h`
- Status: [x]
- Diff: `linux2.6/include/linux/memlayout.h` does not exist (path mismatch)
- Linux Target(s): `arch/x86/include/asm/pgtable.h`
- Plan: 把 x86 内存布局 inline helper 并入 asm/pgtable.h，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/minixfs.h`
- Status: [x]
- Diff: `linux2.6/include/linux/minixfs.h` does not exist (path mismatch)
- Linux Target(s): `fs/minix/minix.h`
- Plan: Lite 当前 minix helper 已在 fs/minix/minix.h；把引用收敛到该内部头并删除旧头。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/page_alloc.h`
- Status: [x]
- Diff: `linux2.6/include/linux/page_alloc.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/gfp.h`, `include/linux/mmzone.h`
- Plan: 把 GFP 类型/分配 API 迁到 `gfp.h`，把 zone/page allocator 统计与初始化接口迁到 `mmzone.h`，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/panic.h`
- Status: [x]
- Diff: `linux2.6/include/linux/panic.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/kernel.h`
- Plan: panic/vsprintf 这类声明在 linux2.6 通常落在 include/linux/kernel.h；将声明并入 kernel.h，删除 panic.h。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/params.h`
- Status: [x]
- Diff: `linux2.6/include/linux/params.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/moduleparam.h`, `include/linux/kernel.h`
- Plan: basename 命中 xen 的 params.h 是误匹配：禁止直接 mv。把 cmdline/module param 相关声明并入 linux2.6 实际存在的头（优先 moduleparam.h），原文件删除并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/pcie.h`
- Status: [x]
- Diff: `linux2.6/include/linux/pcie.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/pci.h`
- Plan: 把当前 PCIe helper 声明并入 pci.h，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/syscall.h`
- Status: [x]
- Diff: `linux2.6/include/linux/syscall.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/syscalls.h`
- Plan: 对齐到 include/linux/syscalls.h：若内容等价则改名/迁移；否则将现有声明合并到 syscalls.h 后删除 syscall.h。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/version.h`
- Status: [x]
- Diff: `linux2.6/include/linux/version.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/printk.h`
- Plan: 把 linux_banner 声明收敛到 printk.h，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/vmscan.h`
- Status: [x]
- Diff: `linux2.6/include/linux/vmscan.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/mmzone.h`
- Plan: Lite 当前 `vmscan.h` 仅承载 reclaim/kswapd 声明；直接把 `scan_control` 与 reclaim 原型收敛到 `mmzone.h`，删除旧头并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `include/linux/vsprintf.h`
- Status: [x]
- Diff: `linux2.6/include/linux/vsprintf.h` does not exist (path mismatch)
- Linux Target(s): `include/linux/kernel.h`
- Plan: vsprintf/vsnprintf 声明并入 include/linux/kernel.h，删除 vsprintf.h，并批量改 include。
- Verify: `make -j4` + `make smoke-128`

### `lib/kref.c`
- Status: [x]
- Diff: `linux2.6/lib/kref.c` does not exist (path mismatch)
- Linux Target(s): `include/linux/kref.h`
- Plan: 该 linux2.6 树无 lib/kref.c：把实现转为 `kref.h` 的 `static inline`，删除 lib/kref.c 并更新构建入口。
- Verify: `make -j4` + `make smoke-128`

### `lib/libc.c`
- Status: [x]
- Diff: `linux2.6/lib/libc.c` does not exist (path mismatch)
- Linux Target(s): `lib/string.c`, `kernel/printk/printk.c`, `lib/vsprintf.c`
- Plan: 按 Linux 职责拆分：把 string/mem/dup/itoa 实现迁到 `lib/string.c`，把 `printf` 包装迁到 `kernel/printk/printk.c`；`vsnprintf` 留在 `lib/vsprintf.c`，删除 `lib/libc.c` 并更新构建入口。
- Verify: `make -j4` + `make smoke-128`
