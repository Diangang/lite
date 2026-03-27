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
