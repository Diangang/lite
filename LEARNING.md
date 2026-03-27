# Lite Kernel 学习笔记

本文档补充 README 的“为什么/怎么做”，聚焦启动阶段的 initramfs 与 VFS 挂载逻辑。

## 1. Initramfs（cpio newc）如何加载

- **入口位置**：[initramfs.c](file:///data25/lidg/lite/init/initramfs.c#L43-L102)
- **数据来源**：Multiboot 模块（`mods_addr/mod_start/mod_end`）。
- **格式**：cpio newc，逐条解析 `c_namesize/c_filesize/c_mode`。
- **解包策略**：在 rootfs（ramfs）上创建目录或文件。
  - 目录：`vfs_mkdir("/" + name)`
  - 普通文件：`vfs_open` + `vfs_write`
- **结束标记**：文件名为 `TRAILER!!!` 时停止。

整体流程在 [start_kernel](file:///data25/lidg/lite/init/main.c#L91-L103) 中发生：先 `vfs_init()` 挂载 rootfs，再解包 initramfs，最终形成可执行的用户态镜像。

## 2. VFS 挂载与挂载表

- **super_block 入口**：`get_sb` 统一指向 VFS 通用实现，文件系统只实现 `fill_super()`。
- **rootfs 挂载**：`vfs_mount_rootfs("ramfs")` 获取 `sb->s_root` 并建立 `vfsmount`。
- **普通挂载**：`vfs_mount_fs("/proc", "proc")` → `get_sb` → `vfs_mount(path, sb)`。
- **挂载表**：所有挂载点通过 `vfsmount->next` 串成单链表，查询入口是 `vfs_get_mounts()`。

## 3. /proc/mounts 的生成

- **路径**：`/proc/mounts`
- **实现**：`proc_read_mounts` 遍历 `vfs_get_mounts()`，输出 `fstype mountpoint`。
- **作用**：用户态可直接查看当前挂载表，并用于 smoke 测试验证。

## 4. Linux 2.6 风格的任务初始化入口

为了和 Linux 2.6 的 `start_kernel()` 分层一致，Lite OS 提供了 `sched_init()` 与 `fork_init()` 作为框架入口：

- `sched_init()`：负责建立最初的 idle 任务与调度基础（当前实现为调用 `init_task()`）。
- `fork_init()`：预留 fork 相关的全局初始化入口（当前为空壳，后续可加入 task/mm 对象池、PID 分配器等）。

## 4.1 PID 0/1 对齐

- PID 0：`sched_init()` 初始化出的初始任务，`rest_init()` 中进入 idle loop（`hlt` 等待 tick）。
- PID 1：由 `rest_init()` 创建的 `kernel_init` 任务，完成 initcall 与挂载后通过 `task_exec_user("/sbin/init")` 直接进入用户态 init，使 PID 1 的语义更接近 Linux 2.6。

## 5. pt_regs 与 copy_thread

- **命名对齐**：将寄存器现场结构体命名为 `pt_regs`，与 Linux 2.6 一致。
- **copy_thread 语义**：负责在新任务内核栈顶构造一份寄存器现场。
  - fork 路径：从 `parent_regs` 复制并设置 `eax=0`。
  - 内核线程路径：构造“伪中断返回现场”，把 `eip` 设为入口函数。

## 6. task/mm 命名对齐

- `current`：当前正在运行的任务（单核全局指针语义，对齐 Linux 的 `current` 概念）。
- `task_struct`：`pid/comm/parent/thread/fs/files/mm` 等字段命名对齐 Linux 风格。
- `mm_struct`：使用 `pgd`（页目录指针）与 `mmap`（VMA 链表），堆区间用 `start_brk/brk`，栈基址用 `start_stack`。
- `vm_area_struct`：使用 `vm_start/vm_end/vm_flags/vm_next`。
- `THREAD_SIZE`：当前每 task 的内核栈固定为 4KB（1 页）。

## 7. 调度 smoke（用户态）

- 早期内核态的 demo 线程（打印 A/B）已移除，避免把“演示逻辑”常驻在内核初始化路径里。
- 当前调度相关的可观测/可复现验证放在用户态 `/bin/smoke`：
  - `fork` 产生两个子进程；
  - 子进程循环执行 `sleep(ticks)` 与 `yield()`，同时输出字符；
  - 父进程用 `waitpid` 等待并检查退出码，验证调度与阻塞路径闭环。

## 8. task 代码拆分

- `sched.c`：task 全局（`current/task_head` 等）与调度相关（`task_schedule/tick/sleep/yield`、`sched_init/init_task`）。
- `fork.c`：任务创建与 fork 路径（`copy_thread`、`kernel_thread/sys_fork`）。
- `exit.c`：任务退出与回收（`task_exit/wait/kill`）。
- `fs/exec.c`：用户程序加载与 `exec`/`enter_user_mode` 路径（ELF 装载、页表与 VMA 初始化）。
- `fs/procfs/base.c`：`/proc` 基础导出（cwd 相关、fd 相关、通用格式化）。
- `fs/procfs/array.c`：`/proc` 基础信息导出（tasks/stat/cmdline/status）。
- `fs/procfs/task_mmu.c`：`/proc` 与地址空间相关导出（maps）。
- 头文件对齐 Linux 2.6：不再提供 `include/*.h` 的扁平聚合头，按职责拆到 `include/linux/{sched,mm,wait,fork,exit,binfmts,fdtable,cred,pid,syscall,fs,file,...}.h` 与 `include/asm/{processor,ptrace,irqflags,unistd,multiboot,gdt,idt}.h`。
- `mm/mmap.c`：用户态地址空间管理相关（`mm_create/mm_destroy`、`sys_mmap/sys_munmap/sys_brk`、VMA/heap/brk 逻辑）。
- `fs/fdtable.c`：文件描述符表管理（`get_unused_fd/fget/close_fd`、stdio 安装、clone/close_all）。
- `arch/x86/kernel/irq.c`：中断开关封装（`irq_save/irq_restore`）。
- `kernel/pid.c`：按 pid 查找 task（精简版）。
- `kernel/cred.c`：uid/gid/umask 等“凭据/权限”相关接口（精简版）。
- `include/linux/sched.h`：task 结构与调度相关常量/全局（对齐 Linux 习惯的头文件命名）。
- `include/linux/mm.h`：`mm_struct`/`vm_area_struct` 与 VMA 标志位定义。
- `include/linux/wait.h`：wait queue 类型与接口声明。
