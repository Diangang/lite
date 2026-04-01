# Linux 2.6 对齐迁移矩阵（x86 32 位）

本矩阵以仓库根目录的 `linux2.6/` 为唯一对齐基准，用于把 Lite OS 当前实现逐文件映射到 Linux 2.6 的目录/文件，并标注对齐状态与下一步迁移动作。

状态说明：
- Match：路径/命名/关键语义基本一致
- Partial：路径/命名接近，但实现语义有缺口或合并/拆分粒度不同
- Diverged：当前实现与 Linux 2.6 差异较大，需要重构/替换
- Missing：Lite 缺失该文件/机制

## arch（x86 32 位）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| arch/x86/boot/boot.s | arch/i386/boot/setup.S、arch/i386/kernel/head.S | Diverged | Lite 采用 multiboot 入口；若追求更“2.6 原生”需逐步向 i386 启动链路/分层收敛 |
| arch/x86/kernel/interrupt.s | arch/i386/kernel/entry.S（中断入口相关） | Partial | Lite 自定义 ISR/IRQ 入口；后续对齐入口栈布局与 pt_regs 语义 |
| arch/x86/kernel/gdt.c、idt.c、isr.c、irq.c | arch/i386/kernel/traps.c、irq.c 等 | Partial | 文件拆分粒度与 Linux 不同；需对齐关键结构与入口符号 |
| include/asm/* | include/asm-i386/* | Partial | Lite 使用 include/asm；Linux 2.6 为 asm-i386/asm-generic 分层 |

## init（初始化）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| init/main.c | init/main.c | Partial | 主流程结构相近；需逐步对齐 initcall 分级、param 解析、ksysfs、driver_init 等 |
| init/version.c | init/version.c | Partial | 版本信息与 utsname 相关语义待补齐 |
| init/initramfs.c | init/initramfs.c | Partial | Lite 已有 cpio newc 解包；需对齐 populate_rootfs/parse_header 等语义边界 |

## kernel（核心机制）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| kernel/sched.c | kernel/sched.c | Partial | Lite 为简化调度；逐步对齐 O(1) runqueue、prio/timeslice 语义（单核可先收敛） |
| kernel/fork.c | kernel/fork.c | Partial | 进程复制/地址空间复制语义需对齐（mm_struct/vma/cow） |
| kernel/exit.c、wait.c、signal.c | kernel/exit.c、kernel/exit.c、kernel/signal.c | Partial | wait/signal 边界语义持续对齐与补测 |
| kernel/syscall.c、syscalls.h | arch/i386/kernel/syscall_table.S、include/asm-i386/unistd.h | Partial | 系统调用表/ABI 与 i386 2.6 风格继续收敛 |
| kernel/printk.c、panic.c | kernel/printk.c、kernel/panic.c | Partial | printk 级别、ring buffer、console 驱动对齐可逐步推进 |

## mm（内存管理）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| mm/page_alloc.c | mm/page_alloc.c | Partial | buddy/zone 语义需对齐，补齐迁移类型与统计 |
| mm/vmalloc.c | mm/vmalloc.c | Partial | vmap 区域管理与页表映射细节待对齐 |
| mm/slab.c | mm/slab.c | Diverged | Lite slab 是学习版；后续选择 slab/slub 任一对齐路径 |
| mm/filemap.c | mm/filemap.c | Partial | page cache/writeback 关键链路待建立 |
| mm/rmap.c、mm/swap.c、mm/vmscan.c | mm/rmap.c、mm/swap*.c、mm/vmscan.c | Partial | rmap/swap/LRU 回收目前为骨架，需逐文件对齐关键函数 |

## fs（VFS/伪文件系统）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| fs/namei.c、fs/file.c、fs/open.c、fs/read_write.c | fs/namei.c、fs/file_table.c、fs/open.c、fs/read_write.c | Partial | VFS 路径解析/引用计数/错误码语义持续对齐 |
| fs/ramfs/ramfs.c | fs/ramfs/inode.c | Partial | 文件粒度不同；后续拆分并对齐 super/inode 操作表 |
| fs/procfs/* | fs/proc/* | Partial | procfs 2.6 结构更复杂；先对齐关键节点语义与遍历 |
| fs/sysfs/sysfs.c | fs/sysfs/* | Partial | /sys/bus/{platform,pci}/{devices,drivers} 已按 bus 视角枚举，/sys/devices 开始按 parent/child 形成层级视图；完整 sysfs link/attr 语义仍待补齐 |
| fs/devtmpfs/devtmpfs.c | drivers/base/devtmpfs.c（不同版本位置差异） | Partial | 以语义对齐为主，后续再精确对齐路径 |

## drivers（设备模型与核心驱动）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| drivers/base/* | drivers/base/* | Partial | 已将 bus/dev/driver/class 的核心挂接链表切到 list_head 风格，并将 kset→kobject 关系从单链表迁移为 list_head；已引入 platform 根设备并自动为 platform 设备设置 parent；uevent/sysfs 的完整语义仍待逐步补齐 |
| drivers/tty/* | drivers/tty/* | Partial | tty core/ldisc/tty_io 等语义与结构需继续拆分对齐 |
| drivers/video/console/* | drivers/video/console/* | Partial | console/vt/tty 关系后续对齐 |
| drivers/pci/* | drivers/pci/* | Partial | PCI 枚举与配置空间语义逐步对齐（qemu 设备为主要验证） |
| drivers/pci/* | drivers/pci/* | Partial | 已引入 pci0000:00 根设备（pci bus）并将 PCI 枚举设备挂到其下，/sys/devices 层级更接近 Linux 风格 |
| drivers/nvme/nvme.c | drivers/block/nvme*（2.6 时代 NVMe 不存在） | Partial | NVMe 不属于 Linux 2.6，但在 Lite 目标中需要；已实现基本的控制器初始化、命名空间管理和块设备注册，参考后续 Linux 的 NVMe 实现风格 |

## lib（通用库）

| Lite | Linux 2.6 | 状态 | 说明/下一步 |
|---|---|---:|---|
| lib/rbtree.c、include/linux/rbtree.h | lib/rbtree.c、include/linux/rbtree.h | Partial | 当前为对齐实现的学习版，后续进一步一致化细节与辅助接口 |
| lib/bitmap.c、include/linux/bitmap.h | lib/bitmap.c、include/linux/bitmap.h | Partial | 接口已建立，后续补齐更多 bitmap API |
| lib/parser.c、include/linux/parser.h | lib/parser.c、include/linux/parser.h | Partial | 先落地最小可用 token/number 解析，后续补齐完整 pattern 语义 |
| lib/idr.c、include/linux/idr.h | lib/idr.c、include/linux/idr.c、include/linux/idr.h | Diverged | 当前实现为简化数组；Linux 2.6 的 idr 基于 radix-tree，需要后续替换为真实实现 |
| include/linux/list.h | include/linux/list.h | Partial | 已补齐 list/hlist 基础操作，后续补齐更多宏与 rcu 变体 |

## 下一步迁移（建议顺序）
1. 补齐基础头：`include/linux/spinlock.h`、`include/linux/atomic.h`、`include/linux/uaccess.h` 关键语义对齐
2. 迁移 radix-tree（为真实 idr 做准备）：`include/linux/radix-tree.h` + `lib/radix-tree.c`（根据当前 mm/slab 能力裁剪）
3. 收敛启动链路的文件粒度：逐步把 `arch/x86` 重构为更接近 `arch/i386` 的分层
