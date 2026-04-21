# Lite Kernel

Lite 是一款用于学习和演示操作系统底层原理的极简 32-bit x86 内核，并在**命名/落位/流程**上持续向 Linux 2.6 靠拢。

如果你想知道“现在系统到底怎么工作”，以 `Documentation/QA.md` 为准；`linux2.6/` 目录是对齐参考快照，不参与构建。

## 快速开始

### 依赖
- Linux (推荐 Ubuntu/Debian)
- `gcc` + `binutils`（需要支持 `-m32`，常见包为 `gcc-multilib`）
- `qemu-system-i386`
- `cpio`（用于打包 initramfs）
- 可选：`grub-mkrescue`（生成 ISO 时需要）

### 构建

```bash
make clean
make -j4
```

产物：
- `out/myos.bin`：内核镜像
- `out/initramfs.cpio`：initramfs（包含 `/sbin/init`、`/sbin/sh`、`/bin/smoke`）

### 运行/调试

```bash
# 运行（默认挂载 virtio-scsi 磁盘，串口交互）
make run

# 冒烟测试（推荐日常用 128M）
make smoke-128

# 进入 GDB 断点等待（QEMU -s -S）
make debug
```

## 当前能力（概览）

这不是 Linux，只是按 Linux 的术语和分层组织的“可运行最小子集”：
- 启动：Multiboot + 早期页表 + 高半区（`arch/x86/boot/header.S`）
- 入口与系统调用：`int 0x80` + 32-bit entry stubs（`arch/x86/entry/entry_32.S`）
- GDT/TSS 与异常/IDT：按 Linux 落位拆分到 `arch/x86/kernel/cpu/common.c` 与 `arch/x86/kernel/traps.c`
- 中断：PIC(i8259) 路径可用；APIC/IO-APIC 目前为占位分层
- 内存：bootmem -> zones/buddy -> paging/vmalloc -> slab -> vmscan/swap（最小实现）
- 进程：fork/exit/wait、信号、tick 驱动调度、COW
- VFS + 文件系统：ramfs/procfs/sysfs/devtmpfs/minix（最小语义）
- 设备模型：kobject/kset/kref + driver core（bus/class/device/driver）
- 存储：block layer 最小子集 + virtio-scsi + NVMe（可跑读写与文件系统 smoke）
- 用户态：PID1 直接 `exec` 到 `/sbin/init`，内置 `/bin/smoke` 做集成验证

更细节的“当前行为说明”和“与 Linux 2.6 的差异”请看：
- `Documentation/QA.md`
- `Documentation/Linux26-Subsystem-Alignment.md`

## 目录结构（入口）

更详细的目录地图见 `Documentation/Directory-Structure.md`。这里只保留最常用入口：
- `arch/x86/`：x86 入口、异常/IRQ、链接脚本
- `init/`：`start_kernel`、initramfs 解包
- `kernel/`：调度/进程/信号/printk/panic/params
- `mm/`：bootmem/zones/page_alloc/paging/vmalloc/slab/vmscan/swap
- `fs/`：VFS + ramfs/procfs/sysfs/devtmpfs/minix
- `block/`：blk-core + blk-sysfs + genhd（最小）
- `drivers/`：driver core、pci/nvme/scsi/virtio/tty/input/clocksource
- `usr/`：用户态程序与 initramfs 载荷
- `Documentation/`：权威文档与对齐进度
- `linux2.6/`：Linux 2.6 参考快照（只读对照）

## 开发约定（对齐优先）

本仓库在做 Linux 2.6 对齐时遵循：
- Reference-first：先读 `linux2.6/` 里的对应文件/符号，再改 Lite
- Same symbol, same file：同名/同义对象必须落在 Linux 对应文件
- 每次改动后至少跑 `make -j4`，建议跑 `make smoke-128`

对齐的执行清单与进度跟踪：
- `Documentation/Linux26-Fixed-Execution-Checklist.md`
- `Documentation/linux-alignment-ledger/placement-diff-plan.md`
