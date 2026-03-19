# Lite OS → 32-bit Linux-like Kernel Roadmap (v4)

目标：实现一个 **32 位、最小可用（minimal usable）的 Linux-like 系统**，以“自己实现一遍”来学习 Linux 最底层细节。重点不是追求完整兼容，而是形成稳定可扩展的对象模型与 ABI：**process/mm、file/vfs、device、tty、signals、block/cache/fs**。

v4 说明：在 v3 基线之上，结合最新实现（ramfs 根、用户态 shell、procfs 扩展）与讨论结论（PID1/init、TTY/ioctl、signals、mmap/COW 等）做一次权衡后的最终路线图。核心策略：先把系统做成“像 Linux 一样能自举的用户态”，再进入 VFS 对象化、设备模型与存储主线。

---

## 0. 当前实现基线（v4）

已具备（可用能力）：

- **Trap/调度基础**：syscall trap gate；`tss.esp0` per-task 更新；tick 驱动时间片与抢占；waitqueue + `task_wait` 阻塞闭环。
- **MM（VMA 驱动）**：VMA 统一描述用户空间；缺页按 VMA 校验权限并按需分配；`brk` 最小版本；退出按 VMA 回收用户页与页表页。
- **VFS（最小 mount tree）**：
  - initrd/procfs/devfs/sysfs + mount 表。
  - **`/` 为可写 ramfs**，initrd 挂到 `/initrd`；根目录可列出挂载点。
  - 路径归一化支持 `.`/`..` 与多重 `/`；cwd 用于 shell 演示。
- **fd 风格 syscall**：`open/read/write/close` + per-task fdtable，stdin/stdout/stderr 默认绑定 `/dev/console`。
- **procfs/sysfs 可观测性**：`/proc/tasks /proc/sched /proc/irq /proc/maps /proc/meminfo`，以及 `/proc/<pid>/{maps,stat,cmdline,status,fd/*}`；sysfs 提供 kernel/version/uptime 与 devices 基础节点。
- **用户态最小闭环**：`ush.elf` 用户态 shell 原型（依赖 `open/read/write/close + chdir/getcwd/getdent/mkdir`）。

已知结构性欠账（会阻塞后续阶段）：

- cwd/namespace 仍偏最小（当前实现并非 per-task/per-process）。
- VFS 对象模型尚未形成完整 inode/dentry/sb/mount 生命周期与缓存语义，目录遍历 ABI 仍偏临时（getdent）。
- 终端仍是“单 console”级别，缺 ioctl/tty line discipline/signal，用户态交互与作业控制无法对齐 Linux。
- 用户态系统缺少 PID1/init 语义与 `exec/wait` 用户态闭环（仍依赖内核 shell `run` 进行拉起）。
- kthread/mm 未分离（内核线程也持 mm），会导致地址空间归属不清、回收与 future COW 语义复杂化。

---

## 1. v4 总原则（以“最小可用 Linux”对齐）

- **最小可用定义**：QEMU 启动后进入用户态 PID1/init，PID1 能拉起交互 shell，能运行基础用户程序；进程崩溃不带崩内核；核心状态可通过 `/proc` `/sys` 观测。
- **先系统后功能**：优先让用户态能自举（PID1/init → shell），再扩展存储与更复杂的内核子系统。
- **对象模型优先**：process/mm、file/vfs、device/driver、tty、block 是长期主干；任何功能必须挂在对象模型上。
- **ABI 收敛优先**：syscall、ioctl、/proc、/sys 的接口一旦暴露，尽量避免频繁破坏式改动。
- **可观测性必须前置**：每个阶段都要能用 `/proc` `/sys` 看见关键状态，避免“黑箱调试”。
- 参考：当前实现对齐 Linux 2.6 子系统的梳理与后续规划见 [linux26_minimal_plan.md](./linux26_minimal_plan.md)。

---

## 2. Phase A：用户态自举（从“run 程序”到“系统入口”）

目标：把 Lite OS 从“内核里有 shell / 手工 run”推进到“内核启动用户态 init（PID1），由 PID1 拉起 shell 与基础程序”。

### A1 per-task CWD / 最小命名空间

- 目标：cwd 从全局变量迁移为 per-task（后续再演进到 per-process namespace）。
- 验收：
  - 两个用户进程并发运行时 cwd 不互相污染。
  - `/proc/<pid>/cwd`（可后补）能观测 cwd。

### A2 kthread/mm 分离（明确内核线程地址空间语义）

- 目标：内核线程不再持有 user mm（或显式绑定 `kernel_mm`），保证：
  - user 任务必有独立 mm；
  - kthread 只运行在 kernel address space，不参与用户页表生命周期。
- 影响（为什么要做）：
  - 避免 kthread “看似有 mm 但不是 user mm” 的语义歧义，阻塞 `/proc/<pid>/maps`、namespace 与权限扩展。
  - 简化回收路径与后续 `fork/COW` 的 mm 引用计数语义，降低误释放与安全风险。
- 验收：
  - `/proc/<pid>/maps` 对 kthread 输出空或 kernel-only 标识。
  - 调度切换时 kthread 不会误切到用户页表。

### A3 exec/wait 用户态闭环（先不做 fork）

- 目标：提供最小 `execve`（替换当前映像）与 `waitpid`（用户态回收）。
- 验收：
  - 用户态 init 能 `execve("/initrd/ush.elf")` 并作为交互入口。
  - `waitpid` 能回收子进程并返回退出码/原因。

### A4 PID1/init 语义

- 目标：内核启动时直接运行用户态 `init.elf`（PID1）；内核态 shell 退化为 debug console。
- 验收：
  - QEMU 启动后默认进入用户态 init 输出，然后进入 shell。
  - PID1 存活时，用户态程序崩溃不会带崩内核。
  - init 采用 fork+wait 作为最小 reaper，shell 退出后可重启。

---

## 3. Phase B：TTY/ioctl/signals（把交互做成 Linux 的样子）

目标：把用户态 shell 的交互从 demo 变成可扩展的控制台系统：稳定的阻塞读、行规程、Ctrl-C、基础作业控制（可简化）。

### B1 ioctl 框架（最小）

- 目标：为字符设备与 tty 建立统一 ioctl 入口，避免后续设备各自加 syscall。
- 验收：
  - `ioctl(fd, ...)` 可用于查询/配置 console 参数（哪怕仅返回固定值）。

### B2 终端子系统（tty + line discipline）

- 目标：从单 `/dev/console` 升级到最小 tty 抽象：
  - canonical/raw、回显、退格/行编辑、读写阻塞语义稳定。
- 验收：
  - 用户态 shell 无需内核前台标志也能稳定读取输入。
  - `cat`/`ush` 在串口与 VGA 两种输入输出路径一致工作。

### B3 signals（最小）

- 目标：提供最小信号投递与处理，用于：
  - Ctrl-C 中断前台进程
  - `kill(pid, sig)` 与 `SIGCHLD`（可简化）
- 验收：
  - 用户态能中断阻塞 read。
  - shell 能杀死前台程序并恢复提示符。

---

## 4. Phase C：MM 扩展（mmap/munmap → fork/COW）

### C1 mmap/munmap（匿名映射优先）

- 目标：引入匿名映射区，减少对 brk 的依赖；为动态加载/栈保护等铺路。
- 验收：
  - 用户态能 mmap 一段匿名内存读写并 munmap 回收。
  - `/proc/<pid>/maps` 反映 mmap 区域并与缺页行为一致。

### C2 Copy-on-Write（fork 前置）

- 目标：实现 COW 页与引用计数，为 fork 提供可接受的性能与语义。
- 验收：
  - fork 后父子进程写时触发 COW，`/proc/<pid>/maps` 与统计能观测复制次数。

---

## 5. Phase D：VFS 对象模型补齐（存储前置条件）

目标：把 VFS 从 “fs_node 包装 + 简化 mount” 升级为可扩展对象模型。

- 关键项：
  - inode/dentry/superblock/mount 的生命周期与引用计数
  - 目录遍历接口收敛（从 getdent 过渡到更通用的 getdents 语义）
  - 权限与身份（uid/gid/mode/umask）最小闭环
- 验收：
  - procfs/sysfs/ramfs/initrd/devfs 全部走统一 VFS 打开实例语义。
  - 用户态 `ush` 的 `ls` 使用 `getdents` 批量遍历目录项并能正常输出。
  - 权限最小闭环：mkdir/open/read/write/chdir 会进行 uid/gid/mode/umask 权限判断；chmod 受 owner/root 限制。
  - 反复打开/遍历/关闭不会造成内核堆持续增长（可用 `/proc/meminfo` 观测）。

---

## 6. Phase E：设备模型（非网络）

目标：形成 kobject/device/driver/bus 的最小框架，为 block/tty/clock 等设备统一管理与可观测铺路。

- 验收：
  - `/sys/devices` 可以反映设备树（console、ramdisk、后续 block 设备）。
  - 设备 probe/bind/unbind 有最小框架（可以先静态注册）。
  - 每个设备目录至少包含 `type/bus/driver` 三个属性文件用于调试观测。

---

## 7. Phase F：存储主线（Block → Cache → Disk FS）

前置条件：P3（VFS 对象模型）与 P4（设备模型）至少完成最小版本。

- P5.1 block_device + request queue（先 ramdisk 或 virtio-blk）
- P5.2 cache（page cache/buffer cache 最小闭环）
- P5.3 一个磁盘文件系统（建议 ext2/minix，暂不做 journaling）

验收：
- syscall → VFS → cache → block queue → device 完整路径打通，并且 `/proc`/`/sys` 可观测。
