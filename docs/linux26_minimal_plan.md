# Lite OS：对齐 Linux 2.6 的“最简但核心具备”规划

本文档用于把当前 Lite OS 的实现，映射到 Linux 2.6 的子系统对象模型，并给出后续最小可用（minimal usable）Linux-like 内核的推进顺序与验收点。

---

## 1. 当前代码结构梳理（按 Linux 2.6 子系统）

内存布局细节（物理/虚拟/用户态）见 [memory_layout.md](./memory_layout.md)。

### 1.1 启动与体系结构（arch）

- 入口与 Multiboot：内核入口与 multiboot module（InitRD）加载路径集中在 [kernel.c](file:///data25/lidg/lite/kernel.c)
- 分段/特权：GDT/TSS/esp0 更新在 [gdt.c](file:///data25/lidg/lite/gdt.c) / [tss.c](file:///data25/lidg/lite/tss.c)
- 中断与异常：IDT/ISR/IRQ 基础在 [idt.c](file:///data25/lidg/lite/idt.c) / [isr.c](file:///data25/lidg/lite/isr.c) / [interrupt.s](file:///data25/lidg/lite/interrupt.s)
- syscall：`int 0x80` trap gate + copyin/copyout 校验在 [syscall.c](file:///data25/lidg/lite/syscall.c)

Linux 2.6 对应：`arch/x86/kernel/` 的 entry/idt/irq/syscall，语义上已具备“从用户态进入内核、再返回”的基本形态。

### 1.2 进程/调度（sched + task）

- 任务对象：`task_t`（当前更接近“线程 + 进程”合体的 task_struct）在 [task.c](file:///data25/lidg/lite/task.c)
- 调度：tick 驱动 timeslice，循环找 RUNNABLE，按 mm 切换页表并更新 `tss.esp0`（同上）
- 阻塞与唤醒：waitqueue 最小实现 + `task_wait` 回收 ZOMBIE（同上）

Linux 2.6 对应：`kernel/sched.c` + `kernel/exit.c` + `kernel/fork.c` 的最小子集。

现状特点：
- 目前是“单 CPU、简单时间片轮转”，足够支撑后续 VFS/TTY/Block 主线。
- task 与 process 的边界还未对象化（没有明显的 `thread_group`/`mm_struct`/`files_struct` 拆分），后续扩展会遇到语义压力。

### 1.3 内存管理（mm）

- 物理页分配与引用计数：PMM 在 [pmm.c](file:///data25/lidg/lite/pmm.c)
- 页表/缺页/用户拷贝：VMM 在 [vmm.c](file:///data25/lidg/lite/vmm.c)
- VMA：以 VMA 驱动用户空间权限与缺页分配，`/proc/<pid>/maps` 可观测（[task.c](file:///data25/lidg/lite/task.c) / [procfs.c](file:///data25/lidg/lite/procfs.c)）
- fork/COW：基于 PTE_COW + 物理页 refcount（[vmm.c](file:///data25/lidg/lite/vmm.c) / [task.c](file:///data25/lidg/lite/task.c)）

Linux 2.6 对应：`mm/`（`mm_struct`/`vm_area_struct`/`do_page_fault`/`copy_on_write`）的简化子集。

现状特点：
- 逻辑路径完整（syscall → vmm_user_accessible → copyin/out → #PF → VMA 校验 → 映射/修正/COW）。
- 缺少 page cache / buffer cache / writeback；后续做 Block/FS 时必须补上（即 Phase F 的关键前置）。

### 1.4 VFS 与文件对象（fs + file）

- fs_node 抽象：最小 read/write/readdir/finddir/ioctl 在 [fs.c](file:///data25/lidg/lite/fs.c) / [fs.h](file:///data25/lidg/lite/fs.h)
- fd/file：per-task fdtable + file offset 在 [file.c](file:///data25/lidg/lite/file.c)
- VFS：mount 表 + 路径归一化 + `open/read/write/chdir/mkdir/chmod` 等在 [vfs.c](file:///data25/lidg/lite/vfs.c)
- 目录遍历 ABI：支持 getdents（用户态 ush `ls` 批量解析）在 [syscall.c](file:///data25/lidg/lite/syscall.c) / [ushprog.s](file:///data25/lidg/lite/ushprog.s)
- 文件系统实例：
  - initrd（只读）[initrd.c](file:///data25/lidg/lite/initrd.c)
  - ramfs（可写）[ramfs.c](file:///data25/lidg/lite/ramfs.c)
  - procfs/sysfs/devfs（伪 fs）[procfs.c](file:///data25/lidg/lite/procfs.c) / [sysfs.c](file:///data25/lidg/lite/sysfs.c) / [devfs.c](file:///data25/lidg/lite/devfs.c)

Linux 2.6 对应：`fs/` 的 inode/dentry/file/sb/mount 对象模型的“外壳”，当前仍有较多简化（例如：inode 的缓存一致性、dentry 负缓存、mount namespace、rename/link/unlink 等）。

### 1.5 设备模型（driver model）

- 最小 kobject/device/driver/bus 框架：[device_model.c](file:///data25/lidg/lite/device_model.c) / [device_model.h](file:///data25/lidg/lite/device_model.h)
- sysfs 对设备树可观测：`/sys/devices/<dev>/{type,bus,driver}`（[sysfs.c](file:///data25/lidg/lite/sysfs.c)）
- 当前默认注册：`console/initrd/ramfs`（[kernel.c](file:///data25/lidg/lite/kernel.c)）

Linux 2.6 对应：`drivers/base/` + `lib/kobject.c` 的最小子集。当前更像“platform bus + 静态注册”，符合 Phase E 的最小验收目标。

### 1.6 TTY/console（交互）

- `/dev/console` 字符设备：read 阻塞 + write 输出 + ioctl flags（[devfs.c](file:///data25/lidg/lite/devfs.c)）
- line discipline/前台：目前主要由 shell/console 逻辑实现（[shell.c](file:///data25/lidg/lite/shell.c)）

Linux 2.6 对应：`drivers/tty/` + `fs/char_dev.c` 的极简形态。后续要把“shell 逻辑”下沉为 tty 子系统，才能稳定支持 signals/job control。

---

## 2. 目标定义：最简 Linux-like 内核“核心要具备”的清单

以 Linux 2.6 视角，最小但核心必须具备的对象模型与链路（按优先级）：

1. **process/mm**：fork/exec/exit/wait + VMA 缺页 + COW + /proc 可观测
2. **file/vfs**：fd + file offset + path walk + 统一对象模型（inode/dentry/sb/mount）最小一致
3. **tty + signals**：阻塞读稳定、Ctrl-C 可中断阻塞 read、最小作业控制（前台/后台可简化）
4. **device model**：device/driver/bus 最小框架 + /sys 可观测 + 为 block/tty/clock 铺路
5. **block + cache + 磁盘 FS**：块设备 + 请求队列 + cache（page/buffer）+ 一个磁盘文件系统（ext2/minix）

---

## 3. 后续规划（建议顺序与验收点）

### 3.1 Phase E（设备模型）下一步：从“可观测”到“可绑定”

- 目标：
  - driver 注册后可自动匹配设备并调用 probe（match 可先按 type 字符串）
  - 支持最小 unbind/rebind（用于调试与后续热插拔路径）
- 验收：
  - `/sys/devices/<dev>/driver` 在绑定后能反映 driver 名（从 `unbound` 变更）
  - 提供一个示例 driver（例如 `console` driver）证明 probe/remove 流程可走通

### 3.2 Phase B（TTY/ioctl/signals）：把交互做成 Linux 的样子

- 目标：
  - tty 对象化：`struct tty_struct` + line discipline（canonical/raw）+ 前台控制
  - signals：至少具备 SIGINT/SIGCHLD；Ctrl-C 能中断阻塞 read
  - ioctl：稳定扩展点（tty attrs、前台 pid、回显开关）
- 验收：
  - `ush`/`cat` 的 read 阻塞不靠忙等，Ctrl-C 可打断并返回提示符
  - `kill(pid, SIGINT)` 可用；SIGCHLD 能驱动 wait 回收而不泄漏

### 3.3 Phase A（用户态自举）收敛：弱化 kernel shell

- 目标：
  - kernel shell 退化为 debug console；系统入口由 PID1/init 负责
  - 子进程退出后不会永久 ZOMBIE（init 负责 reaper 或内核做最小 reparent）
- 验收：
  - 无需内核命令 `run` 即可启动并维持用户态 shell
  - `/proc/tasks` 不会无限增长 ZOMBIE（可在测试脚本里反复 fork/exec/exit 验证）

### 3.4 Phase F（存储主线）：Block → Cache → Disk FS

- 目标（最小路径）：
  - block_device（ramdisk/virtio-blk 二选一，建议先 ramdisk）
  - request queue（单队列，先 FIFO；后续再引入 elevator）
  - cache：优先做 page cache 的最小闭环（read-ahead 可后补）
  - 磁盘 FS：ext2/minix 二选一（建议 ext2 子集）
- 验收：
  - syscall → VFS → cache → block queue → device 全路径打通
  - `/sys/devices` 中能看到 block 设备；`/proc` 能看到 I/O 统计（最小版）
