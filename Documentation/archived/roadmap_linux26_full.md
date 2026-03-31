# Lite OS → Linux 2.6 全量对齐 Roadmap

目标：在 **x86 32 位范围内**，内核子系统、机制与目录结构尽可能与 Linux 2.6 保持一致（参考 `/data25/lidg/axboe`），并保证**文件名、文件夹与目录结构**与 Linux 保持一致，形成可对照的学习型实现。

---

## 0. 现状概览（Lite）

- 仅支持 `arch/x86`，最小引导 + trap/调度 + 简化 mm/vfs/driver model。
- 文件系统：ramfs/procfs/sysfs/devtmpfs（最小）。
- 设备模型：单一 platform bus，极简 kobject/device/driver。
- TTY/console：最小 tty + console + serial + vga。
- 缺失：完整 kobject/sysfs、block/cache、网络栈、IPC、security、模块加载等。

---

## 1. 模块对齐差距清单（按 Linux 2.6 目录）

### 1.1 arch/
**现状**：仅 `arch/x86` 最小中断/分页/TSS/GDT/IDT/IRQ。
**缺失**：
- x86 内部结构与启动链路对齐（start_kernel/entry/irq 子路径）。
- `cpu/irq/irqflags/entry` 机制完整化（per-cpu、软中断）。
- 设备树/ACPI 入口（platform 设备来源）。

### 1.2 init/
**现状**：`main.c` 简化 initcall 与启动流程。
**缺失**：
- Linux 2.6 的 `start_kernel` 全流程（initcall 分级、param 解析、kmem cache 初始化、ksysfs）。
- `init/` 目录结构与版本信息（`version.c`）。

### 1.3 kernel/
**现状**：sched/fork/exit/pid/syscall/cred 基础。
**缺失**：
- 完整调度器（O(1) 2.6 风格），优先级与 RT 语义（单核可先不做负载均衡）。
- signal/ptrace/time/timer/workqueue/kthread。
- module 加载（kmod/module.c）与参数体系。
- uevent/hotplug 与内核参数（sysctl）。

### 1.4 mm/
**现状**：page_alloc/pgtable/mmap/文件映射基础。
**缺失**：
- 页面回收与 LRU（vmscan）、内存回写（可后置）。
- slab/SLUB、buddy、highmem、vmalloc、mprotect/mremap（可后置）。
- swap（可后置）。
- 进程匿名页与 page cache 完整联动。

### 1.5 fs/
**现状**：VFS 最小 + ramfs/procfs/sysfs/devtmpfs。
**缺失**：
- 完整 VFS 对象模型：inode/dentry/sb/mount 生命周期与缓存。
- block layer + page cache + buffer cache。
- 文件系统族（ext2/minix）与通用层（buffer.c、block_dev.c、bio.c）。
- sysfs 作为 kobject 自动映射的正式实现（`fs/sysfs/*`）。

### 1.6 drivers/
**现状**：base/core+bus+driver+init + tty/serial + vga + keyboard + timer。
**缺失**：
- driver core 完整化：kobject/kset/kref、class、subsystem、uevent。
- 多总线：PCI/USB/I2C/SPI/virtio。
- block 设备驱动与设备发现。
- 目录结构对齐 Linux（drivers/tty/serial、drivers/video/console、drivers/base 等已开始对齐）。

### 1.7 ipc/
**现状**：缺失。
**缺失**：
- System V IPC（sem/msg/shm）。
- POSIX mqueue。

### 1.8 lib/
**现状**：仅最小 libc。
**缺失**：
- kobject/kref、rbtree、idr、bitmap、crc 等核心工具库。

### 1.9 usr/
**现状**：最小用户态 init/shell/smoke。
**缺失**：
- initramfs 生成流程对齐，最小 userland 工具链体系。

### 1.10 非核心/暂不考虑
**范围**：
- net/security/crypto/sound 子系统。
- 多架构兼容性。
- 复杂磁盘文件系统（ext4/xfs/jfs 等非易失存储）。

---

## 2. 新 Roadmap（按优先级）

### P0：内存基础（必须先稳定）
1. **内存布局与映射约定**：内核/用户空间分割、恒等映射、映射窗口、内核高半区策略
2. **页表与地址空间语义**：用户/内核权限、copy_to/from_user 边界、缺页路径稳定
3. **分配器基础**：page_alloc/pgtable/slab 行为可预测，最小统计与调试
4. **启动期 trampoline 收敛**：缩小低端恒等映射范围，开启分页后立即切换高半区并清理 PDE0
5. **早期可观测性最小化**：仅保留寄存器安全的早期串口/调试输出路径

### P1：启动、调度与可观测性
1. **调度器语义对齐**：O(1) runqueue 结构、优先级语义、时钟驱动时间片
2. **kobject/kset/kref 基础库落地**（lib/ + include）
3. **sysfs 自动映射最小可用**（/sys/devices + /sys/bus + /sys/class 框架）
4. **driver core 重构**：device/driver/bus/class/subsystem 组织一致化

### P2：VFS + Block 主链路
1. 完整 inode/dentry/sb/mount 生命周期与缓存语义
2. bio/block layer + request queue
3. page cache/buffer cache
4. 内存/虚拟块设备上的最小文件系统（后续再评估磁盘 FS）

### P3：MM 子系统对齐（可后置）
1. slab/SLUB/buddy
2. LRU 回收 + vmscan
3. swap + page reclaim
4. mprotect/mremap/vmalloc/highmem

### P4：TTY/Console 对齐
1. tty core + line discipline 标准化
2. vt/console 关系对齐（drivers/video/console + drivers/tty）
3. 串口与虚拟终端多实例

### 调度器语义对齐说明
- 单核 + 非抢占只能做到功能接近，无法做到与 Linux 2.6 机制完全一致。
- 若要机制一致，需要：preempt + 时钟中断抢占点 + 2.6 O(1) 运行队列语义。
- 多核 + 抢占是必要条件，但仍需补齐 O(1) runqueue、SMP 负载均衡、IRQ/softirq 语义与 per-cpu 调度数据。

### P5：多总线与设备发现
1. platform 设备表（最小入口）
2. PCI/virtio 最小枚举

### P6：IPC（非核心但可选）
1. System V IPC
2. POSIX mqueue

---

## 3. 目录结构对齐目标（示意）

- `arch/`：x86 结构对齐 Linux（不做多架构）
- `init/`：start_kernel/driver_init/ksysfs
- `kernel/`：sched/signal/ptrace/time/module
- `mm/`：slab/vmscan/swap/vmalloc
- `fs/`：VFS 全量 + block layer + sysfs 正式实现
- `drivers/`：base/tty/serial/video/console + bus 子系统
- `ipc/`：System V / POSIX（可选）

目录与文件命名约束：
- 目录与文件名尽量与 Linux 2.6 保持一致（参考 `/data25/lidg/axboe`）。
- 新增实现优先对齐 Linux 2.6 的文件位置与命名。

---

## 4. 优先级建议（结合 Lite 现状）

1. **内存基础（P0）**
2. **driver core + sysfs 对齐（P1）**
3. **VFS/Block 主链路（P2）**
4. **MM 回收与 slab（P3）**
5. **TTY/console 深化（P4）**
6. **总线/设备发现（P5）**
7. **IPC（P6，可选）**

---

## 5. 细分任务表（按阶段拆分）

### P0 内存基础
- 内存布局文档化：内核/用户空间边界、恒等映射范围、高半区策略
- 页表权限语义：U/S、R/W、present，copy_to/from_user 边界
- 缺页路径稳定：页错误分类与最小修复路径
- page_alloc/pgtable/slab 行为可预测：分配失败策略与基本统计

### P1 启动、调度与可观测性
- 启动链路对齐：start_kernel → driver_init → ksysfs → rest_init
- initcall 分级：core/subsys/fs/device/late 的最小落地
- 调度器结构：O(1) runqueue（active/expired）
- 调度策略：优先级语义 + 时间片衰减
- 时钟驱动：调度 tick 与时间片消耗一致化
- kobject/kset/kref 最小实现
- sysfs 自动映射框架：/sys/devices、/sys/bus、/sys/class
- driver core：device/driver/bus/class/subsystem 组织与绑定

### P2 VFS + Block 主链路
- inode/dentry/sb/mount 生命周期与引用计数
- VFS 打开/关闭与路径缓存一致化
- block/bio 基础数据结构与提交路径
- page cache/buffer cache 贯通
- 最小块设备（ramdisk/virtio-blk）与挂载闭环

### P3 MM 子系统对齐（可后置）
- slab/SLUB/buddy 任一版本的内核对象分配器
- LRU 回收/vmscan 基础框架
- swap 机制与页回收闭环
- mprotect/mremap/vmalloc/highmem

### P4 TTY/Console 对齐
- tty core + line discipline 正常化
- 控制终端与前台进程语义
- vt/console 关系对齐
- 串口与虚拟终端多实例

### P5 多总线与设备发现
- platform 设备表与资源描述
- PCI/virtio 最小枚举与驱动绑定

### P6 IPC（可选）
- System V IPC：sem/msg/shm
- POSIX mqueue
