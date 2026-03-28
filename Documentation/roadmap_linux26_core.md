# Lite OS → Linux 2.6 Core Roadmap（核心机制优先）

## 目标
- x86 32 位范围内，只保留核心子系统，目录/文件/函数命名尽量对齐 Linux 2.6
- 优先实现“核心机制”：调度/退出/等待、VMA/缺页/回收、VFS 内存路径、syscall 边界语义
- 暂不涉及 net/security/crypto/sound 与多架构

## 现状（已具备）
- 启动链路：`start_kernel → rest_init → kernel_init → do_basic_setup → prepare_namespace`
- 内核核心：`sched/fork/exit/pid/syscall/cred` 基础语义可用
- 内存框架：`bootmem/page_alloc/vmalloc/slab/vmscan` 基础骨架可用
- VFS 框架：ramfs/procfs/sysfs/devtmpfs 已可挂载
- 关键语义：`waitpid` 返回回收 PID；`mprotect/mremap` 最小语义；uaccess 按 VMA 允许触发缺页
- 用户态验证：`/bin/smoke` 覆盖 fork/waitpid/mmap/mprotect/mremap 与调度

## 核心缺口（只列关键机制）
- MM：rmap 与匿名页管理、swap 最小闭环、page cache/writeback
- VFS：内存路径完善（页缓存与 VFS 交互）
- driver core：class/uevent/kset 的最小语义对齐
- lib 基础：rbtree/idr/bitmap/parser
- 块设备：暂缓，仅保留规划

## 后续计划（按核心机制优先级）

### P0 语义对齐与命名清理（立即）
- 结构体/函数/文件命名与 Linux 2.6 对齐，保证可对比性
- 保持目录布局：`arch/ init/ kernel/ mm/ fs/ drivers/ lib/ include/`
- 状态：已闭环

### P1 进程/调度/时间基线
- `time.c/timer.c` 语义对齐
- `signal/wait` 语义闭环与边界一致
- 调度器时间片与阻塞/唤醒路径稳定
- 状态：已闭环

### P2 MM 深化（核心）
- rmap 与匿名页管理
- swap 基础闭环
- page cache/writeback 最小闭环

### P3 VFS（以内存文件系统为主）
- ramfs/devtmpfs/procfs/sysfs 语义补齐
- VFS 与 page cache 内存路径贯通

### P4 driver core 完整化
- class/uevent/kset 语义对齐
- sysfs 映射与 device model 统一

### P5 块设备主链路（推迟）
- `block_dev.c/bio.c/buffer.c` 框架
- 最小块设备驱动 + 最小块 FS（ext2 或 minix）
- page cache/writeback 与块层贯通

## 最小文件清单（目标）
```
arch/x86/...
init/main.c
init/version.c
kernel/ksysfs.c
kernel/printk.c
kernel/panic.c
kernel/time.c
kernel/timer.c
kernel/signal.c
kernel/wait.c
mm/buffer.c
fs/block_dev.c
fs/bio.c
fs/buffer.c
drivers/block/ramdisk.c
lib/rbtree.c
lib/idr.c
lib/bitmap.c
lib/parser.c
```

## 验收标准（按阶段）
- P1：`signal/wait` 语义稳定，调度与时间片行为可复现
- P2：rmap/swap/writeback 有最小闭环，缺页/回收路径可验证
- P3：VFS 内存路径稳定，`/proc` 与 `sysfs` 语义一致
