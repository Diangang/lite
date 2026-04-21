# Lite OS → Linux 2.6 Core Roadmap（核心机制优先）

## 目标
- x86 32 位范围内，只保留核心子系统，目录/文件/函数命名与 Linux 2.6 对齐
- 优先实现“核心机制”：调度/退出/等待、VMA/缺页/回收、VFS 内存路径、syscall 语义
- 暂不涉及 net/security/crypto/sound 与多架构

## 对齐基准
- 参考源码：`linux2.6/`（同目录链接）
- 仅对齐 x86 32 位路径与命名：`arch/x86`、`init`、`kernel`、`mm`、`fs`、`drivers`、`lib`、`include`

## 现状（基于当前代码）
- 启动链路：`arch/x86/boot/boot.s` + `start_kernel` 主流程已建立，入口与基础初始化存在
- 内核核心：`sched/fork/exit/pid/syscall/cred` 框架存在，语义覆盖仍需补齐与验证
- 内存框架：`bootmem/page_alloc/vmalloc/slab/vmscan` 具备骨架与接口
- VFS 框架：ramfs/procfs/sysfs/devtmpfs 目录与主要入口已具备
- 驱动骨架：PCI/PCIE、TTY/serial、VGA console、clocksource、键盘、NVMe 基础框架
- 用户态：`usr/` 下最小 init/shell/smoke 与 initramfs 生成链路已具备

## 与 Linux 2.6 的主要偏差（需收敛）
- 目录与文件粒度未完全对齐 Linux 2.6（部分功能合并于单文件）
- arch/x86 启动与 setup 流程需进一步对齐 2.6 的分层组织
- mm/vfs/lib 的通用结构仍有缺口（rbtree/idr/bitmap/parser 等）
- driver core 的 device model 语义与 sysfs 映射仍不完整

## 核心缺口（关键机制）
- MM：rmap 与匿名页管理、swap 最小闭环、page cache/writeback
- VFS：页缓存与 VFS 的内存路径贯通
- driver core：class/uevent/kset 的最小语义对齐
- lib 基础：rbtree/idr/bitmap/parser 等通用结构
- 块设备：仅保留规划，尚未形成最小闭环

## 后续计划（按核心机制优先级）

### P0 语义对齐与命名清理（立即）
- 结构体/函数/文件命名与 Linux 2.6 对齐，保证可对比性
- 保持目录布局：`arch/ init/ kernel/ mm/ fs/ drivers/ lib/ include/ usr/`
- 状态：进行中

#### P0 细化任务
- 统一关键符号前缀与导出接口命名（与 2.6 对齐）
- 清理重复或混杂命名（如同一语义多套前缀）
- 统一头文件包含路径与层次（include/linux vs include/asm）
- 对照 `linux2.6` 的文件拆分粒度，拆出缺失模块

#### P0 验收
- 关键路径函数命名可与 Linux 2.6 对照查找
- include 层次无循环依赖

### P1 进程/调度/时间基线
- `time.c/timer.c` 语义对齐
- `signal/wait` 语义闭环与边界一致
- 调度器时间片与阻塞/唤醒路径稳定
- 状态：部分可用

#### P1 细化任务
- 进程生命周期：`do_fork → wake_up_new_task → do_exit → release_task`
- wait 语义：`waitpid` 可正确回收子进程并返回状态
- signal 语义：最小异步信号投递与默认处理
- 时间基线：jiffies/定时器最小闭环，睡眠与唤醒可预测
- 对齐 `kernel/time.c` 与 `kernel/timer.c` 的关键路径命名

#### P1 验收
- `smoke` 中的 fork/wait 回收稳定
- sleep/timeout 行为一致可复现

### P2 MM 深化（核心）
- rmap 与匿名页管理
- swap 基础闭环
- page cache/writeback 最小闭环
- 状态：未完成

#### P2 细化任务
- rmap：VMA 反向映射与基本回收路径
- anon：匿名页生命周期与 COW 语义最小闭环
- swap：swap out/in 最小链路与回收策略
- page cache：file-backed 页与回写路径贯通
- 对齐 `mm/rmap.c`、`mm/swap*.c`、`mm/filemap.c` 的关键函数

#### P2 验收
- 内存压力下缺页与回收可工作
- swap 配置启用后可触发换出与换入

### P3 VFS（以内存文件系统为主）
- ramfs/devtmpfs/procfs/sysfs 语义补齐
- VFS 与 page cache 内存路径贯通
- 状态：进行中

#### P3 细化任务
- VFS 读写路径：open/read/write/lseek/close 一致性
- dentry/inode 缓存：基本引用计数与回收
- procfs/sysfs：核心节点与最小读写语义
- devtmpfs：设备节点最小创建与访问
- 对齐 `fs/namei.c`、`fs/file.c`、`fs/inode.c` 的关键路径

#### P3 验收
- init 进程可加载并访问 /proc 与 /sys
- 文件读写与页缓存一致性可验证

### P4 driver core 完整化
- class/uevent/kset 语义对齐
- sysfs 映射与 device model 统一
- 状态：未完成

#### P4 细化任务
- device/bus/driver 绑定模型最小闭环
- uevent 最小事件模型与 sysfs 触发一致
- kobject/kset 引用计数与生命周期统一
- 对齐 `drivers/base/*` 与 `lib/kobject.c` 的关键语义

#### P4 验收
- 设备注册后 sysfs 节点可见
- 设备绑定/解绑路径可复现

### P5 块设备主链路（推迟）
- `block_dev.c/bio.c/buffer.c` 框架
- 最小块设备驱动 + 最小块 FS（ext2 或 minix）
- page cache/writeback 与块层贯通
- 状态：未开始

#### P5 细化任务
- 块层：bio 提交与完成路径最小闭环
- buffer cache：块缓存与页缓存协同
- 最小块 FS：只读挂载或最小读写路径
- 对齐 `fs/block_dev.c`、`fs/bio.c`、`fs/buffer.c` 的关键路径

#### P5 验收
- 可挂载最小块文件系统并读取文件
- 块设备 IO 与缓存一致性可验证

## 最小文件清单（当前）
```
arch/x86/boot/boot.s
arch/x86/kernel/linker.ld
arch/x86/kernel/{gdt,idt,isr,irq,setup}.c
arch/x86/kernel/interrupt.s
init/{main,version,initramfs}.c
kernel/{sched,fork,exit,pid,cred,syscall,signal,wait,panic,printk,time}.c
mm/{mm,bootmem,mmzone,mmap,page_alloc,vmalloc,slab,vmscan,swap,filemap,rmap,memory}.c
fs/{file,fdtable,exec,inode,dentry,namei,read_write,open,readdir,ioctl,namespace}.c
fs/ramfs/ramfs.c
fs/procfs/{procfs,base,array,task_mmu}.c
fs/sysfs/sysfs.c
fs/devtmpfs/devtmpfs.c
drivers/base/{core,bus,driver,init}.c
drivers/pci/{pci,pcie/pcie}.c
drivers/tty/{tty,serial/serial}.c
drivers/video/console/{vga,console,console_driver}.c
drivers/clocksource/timer.c
drivers/input/keyboard.c
drivers/nvme/host/pci.c
usr/{crt0,ulib,init,shell,smoke}.c
usr/ulinker.ld
```

## 验收标准（按阶段）
- P1：`signal/wait` 语义稳定，调度与时间片行为可复现
- P2：rmap/swap/writeback 有最小闭环，缺页/回收路径可验证
- P3：VFS 内存路径稳定，`/proc` 与 `sysfs` 语义一致
