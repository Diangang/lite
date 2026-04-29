# Lite Kernel 学习笔记

本文档补充 README 的“为什么/怎么做”，聚焦启动阶段的 initramfs 与 VFS 挂载逻辑；P0 命名/布局对齐已闭环。

## 1. Initramfs（cpio newc）如何加载

- **入口位置**：[initramfs.c](file:///data25/lidg/lite/init/initramfs.c#L43-L102)
- **数据来源**：Multiboot 模块（`mods_addr/mod_start/mod_end`）。
- **格式**：cpio newc，逐条解析 `c_namesize/c_filesize/c_mode`。
- **解包策略**：在 rootfs（ramfs）上创建目录或文件。
  - 目录：`vfs_mkdir("/" + name)`
  - 普通文件：`vfs_open` + `vfs_write`
- **结束标记**：文件名为 `TRAILER!!!` 时停止。

整体流程在 [start_kernel](file:///data25/lidg/lite/init/main.c#L81-L101) 与 [kernel_init](file:///data25/lidg/lite/init/main.c#L45-L55) 中分层完成：`start_kernel()` 完成架构与内存/调度初始化后进入 `rest_init()`，`kernel_init()` 先执行 `do_basic_setup()`，再由 `prepare_namespace()` 完成 VFS/initramfs 挂载与 `ksysfs_init()` 并进入用户态镜像。

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
- PID 1：由 `rest_init()` 创建的 `kernel_init` 任务，完成 `do_basic_setup()` 与 `prepare_namespace()` 后通过 `task_exec_user(init)` 进入用户态，优先使用 `init=` 指定路径，否则按 `/sbin/init`、`/etc/init`、`/bin/init`、`/bin/sh` 回退。

## 4.2 分级 initcall

- `module_init(fn)` 会把 `fn` 的函数指针放进链接段里（initcall 段），由 `kernel_init` 阶段的 `do_initcalls()` 统一遍历调用。
- 为了对齐 Linux 2.6 的分层启动语义，Lite 将 initcall 分成 6 个级别并按序执行：
  - `early_initcall/core_initcall/subsys_initcall/fs_initcall/device_initcall/late_initcall`
  - `module_init` 当前等价于 `device_initcall`（设备与驱动相关初始化默认放这一层）。
- `do_initcalls()` 的遍历范围只有 `__initcall_start .. __initcall_end`，级别顺序由链接脚本中各 `.initcallN.init` 段的排列顺序保证（对齐 Linux 的做法）。

## 5. pt_regs 与 copy_thread

- **命名对齐**：将寄存器现场结构体命名为 `pt_regs`，与 Linux 2.6 一致。
- **copy_thread 语义**：负责在新任务内核栈顶构造一份寄存器现场。
  - fork 路径：从 `parent_regs` 复制并设置 `eax=0`。
  - 内核线程路径：构造“伪中断返回现场”，把 `eip` 设为入口函数。

## 6. task/mm 命名对齐

- `current`：当前正在运行的任务（单核全局指针语义，对齐 Linux 的 `current` 概念）。
- `task_struct`：`pid/comm/parent/thread/fs/files/mm` 等字段命名对齐 Linux 风格。
- `mm_struct`：使用 `pgd`（页目录指针）与 `mmap`（VMA 链表），堆区间用 `start_brk/brk`，栈基址用 `start_stack`，用户栈默认 8 页。
- `vm_area_struct`：使用 `vm_start/vm_end/vm_flags/vm_next`。
- `THREAD_SIZE`：当前每 task 的内核栈固定为 4KB（1 页）。

## 7. 调度 smoke（用户态）

- 早期内核态的 demo 线程（打印 A/B）已移除，避免把“演示逻辑”常驻在内核初始化路径里。
- 当前调度相关的可观测/可复现验证放在用户态 `/bin/smoke`：
  - `fork` 产生两个子进程；
  - 子进程循环执行 `sleep(ticks)` 与 `yield()`，同时输出字符；
  - 父进程用 `waitpid` 等待并检查退出码，验证调度与阻塞路径闭环。
- `/bin/smoke` 覆盖 `fork/waitpid/mmap/mprotect/mremap` 的最小回归验证。

## 8. task 代码拆分

- `sched.c`：task 全局（`current/task_head` 等）与调度相关（`task_schedule/tick/sleep/yield`、`sched_init/init_task`），`task_yield()` 通过软件触发 IRQ0 进入调度路径，唤醒 waitq 会触发一次调度请求。
- `fork.c`：任务创建与 fork 路径（`copy_thread`、`kernel_thread/sys_fork`）。
- `exit.c`：任务退出与回收（`task_exit/do_exit/do_exit_reason`），退出时对子进程执行 reparent 到 PID 1，同时从 wait 队列移除自身。
- `wait.c`：等待与回收（`do_waitpid/sys_waitpid`），支持 `waitpid(-1)` 等待任意子进程并返回被回收的 PID。
- `signal.c`：信号与退出（`sys_kill`），支持 `sig=0` 仅检查进程存在。
- `/proc/<pid>/status`：新增 `ExitCode/ExitState/Signal` 字段用于观察退出原因。
- `fs/exec.c`：用户程序加载与 `exec`/`enter_user_mode` 路径（ELF 装载、页表与 VMA 初始化）。
- `fs/devtmpfs/devtmpfs.c`：devtmpfs 设备节点（/dev）最小实现，节点由设备模型注册/注销驱动生成（`/dev/console` 对应内核控制台，`/dev/tty` 对应用户态终端）。
- `kernel/syscall.c`：syscall 入口与分发（x86 `int 0x80`），只负责寄存器参数解包、调用 `sys_*`，以及统一调度收尾（`task_should_resched`）；`SYS_SLEEP` 通过 `task_sleep` 进入阻塞并触发调度。
- `arch/x86/kernel/isr.c`：IRQ0 时钟中断内仅在 `task_should_resched()` 为真时触发调度，避免无条件抢占。
- `kernel/printk.c`：`printk` 输出入口，作为内核日志最小封装。
- `kernel/panic.c`：panic 终止路径，封装 `cli/hlt` 并输出错误信息。
- `kernel/params.c`：保存启动命令行到 `saved_command_line`，解析 `init=` 并提供 `get_init_process()`。
- `init/version.c`：提供 `linux_banner`，用于启动期打印版本信息。
- `kernel/time.c`：`jiffies` 与 `time_get_uptime()`，`HZ` 为默认节拍，PIT 驱动更新。
- `include/linux/syscalls.h`：对内的 syscall 实现入口声明（`sys_read/sys_write/...`），用于把实现放在对应子系统文件里。
- `include/linux/uaccess.h`：用户指针访问封装（`copy_to_user/copy_from_user/strncpy_from_user`），未映射但处于合法 VMA 的用户地址允许通过校验以触发缺页分配，内核访问用户地址时按用户缺页语义处理。
- `fs/procfs/base.c`：`/proc` 基础导出（cwd 相关、fd 相关、通用格式化）。
- `fs/procfs/array.c`：`/proc` 基础信息导出（tasks/stat/cmdline/status）。
- `fs/procfs/task_mmu.c`：`/proc` 与地址空间相关导出（maps）。
- 头文件对齐 Linux 2.6：不再提供 `include/*.h` 的扁平聚合头，按职责拆到 `include/linux/{sched,mm,wait,fork,exit,binfmts,fdtable,cred,pid,syscall,fs,file,...}.h` 与 `include/asm/{processor,ptrace,irqflags,unistd,multiboot,gdt,idt}.h`。
- `mm/mmap.c`：用户态地址空间管理相关（`mm_create/mm_destroy`、`sys_mmap/sys_munmap/sys_brk`、VMA/heap/brk 逻辑）。
- `mm/mmap.c`：补充 `sys_mprotect/sys_mremap` 的最小语义（权限变更与 in-place 扩缩）。
- `mm/rmap.c`：最小 rmap 骨架（按页 mapcount 与多映射链表记录），为回收/换出预留接口。
- `mm/swap.c`：最小 swap 出入路径（内存后备，支持缺页换入）。
- `mm/filemap.c`：最小 page cache 数据结构与内存路径，保留简化读写逻辑。
- `fs/fdtable.c`：文件描述符表管理（`get_unused_fd/fget/close_fd`、stdio 安装、clone/close_all）。
- `fs/file.c`：`vfs_open` 在 `O_TRUNC` 时清理文件映射并更新大小。
- `fs/namei.c`：新增 `vfs_rmdir/sys_rmdir`，与 `ramfs` 的空目录删除语义对齐，禁止对 `.`/`..` 删除。
- `fs/ramfs/ramfs.c`：`unlink/rmdir` 同步释放 dentry 与 inode，避免目录树脏节点。
- `drivers/base/init.c`：注册 `console/tty` 类并导出 `/sys/class` 视图。
- `drivers/base/core.c`：最小 uevent 事件流（add/remove）并导出 `/sys/kernel/uevent`。
- `drivers/base/driver.c`：绑定/解绑驱动时补充 uevent。
- `drivers/base/bus.c`：match 语义支持 `device_id` 表（按 name/type 匹配）。
- `drivers/base/driver.c`：新增 `driver_unregister`，解除驱动并解绑设备。
- `drivers/base/driver.c`：驱动注册与绑定增加重复保护。
- `drivers/pci/pci.c`：最小 PCI 总线扫描与设备注册、配置空间读接口。
- `drivers/pci/pci.c`：BAR 探测与基础资源大小解析。
- `drivers/pci/pci.c`：桥设备识别与递归扫描 secondary bus。
- `drivers/pci/pci.c`：桥设备资源窗口解析（IO/MEM/Prefetch）。
- `drivers/pci/pci.c`：为设备 BAR 分配基址并写回配置空间。
- `drivers/pci/pci.c`：分配过程受桥窗口约束，失败触发 barfail 事件。
- `drivers/pci/pci.c`：BAR 分配要求 2 的幂对齐，否则直接失败。
- `drivers/pci/pci.c`：启用 PCI 命令寄存器（IO/MEM/BUS MASTER）并发出 enable 事件。
- `drivers/pci/pcie/pcie.c`：PCIe 框架占位入口。
- `drivers/pci/pcie/pcie.c`：扫描 capability list 并识别 PCIe capability。
- `usr/smoke.c`：新增 PCIe capability 识别自测。
- `usr/smoke.c`：PCIe 识别失败时输出 uevent 内容。
- `drivers/pci/pci.c`：桥设备自动分配 secondary bus，并发出 busnum 事件。
- `drivers/pci/pci.c`：识别 NVMe class 设备并发出 nvme 事件。
- `drivers/nvme/host/pci.c`：NVMe class 设备绑定入口，映射 BAR0 并读取 CAP/VS。
- `arch/x86/kernel/irq.c`：中断开关封装（`irq_save/irq_restore`）。
- `kernel/pid.c`：按 pid 查找 task（精简版）。
- `kernel/cred.c`：uid/gid/umask 等“凭据/权限”相关接口（精简版）。
- `include/linux/sched.h`：task 结构与调度相关常量/全局（对齐 Linux 习惯的头文件命名）。
- `include/linux/mm.h`：`mm_struct`/`vm_area_struct` 与 VMA 标志位定义。
- `include/linux/wait.h`：wait queue 类型与接口声明（`wait_queue_head_t/init_waitqueue_head/wake_up_all`）。

## 9. 内存管理初始化流程

- **入口**：`start_kernel()` 内完成早期内存与分页初始化，顺序是 `bootmem_init → init_zones → build_all_zonelists → free_area_init → paging_init → mem_init → kswapd_init → swap_init → kmem_cache_init`，启动期低端恒等映射覆盖前 4MB 并在 trampoline 中清理，内核主体 VMA 为 `PAGE_OFFSET + 0x00100000 + sizeof(.text.boot)`。
- **bootmem**：只用于早期线性分配与保留内存范围，为 page/zone 数据结构提供可用空间。
- **zone/page**：建立最小 `struct page` 数组与 `zone_dma/zone_normal`，并初始化 `free_area` 与 `zonelist` 作为 buddy 的挂接点，managed_pages 在 mem_init 中收敛。
- **free_area_init/free_area_init_core**：物理页分配主路径初始化，建立 buddy 元数据并标记 `PG_RESERVED`，准备 free_area。
- **watermarks**：`__alloc_pages_nodemask` 依据 HIGH/LOW/MIN 水位选择分配并触发最小回收唤醒点，水位根据 managed_pages 刷新。
- **GFP_DMA**：通过 DMA-only zonelist 将分配限制在 ZONE_DMA。
- **vmscan/kswapd**：预留最小回收入口与唤醒路径。
- **meminfo 分区统计**：`/proc/meminfo` 增加 DMA/Normal 的总量与空闲量输出。
- **vmalloc/ioremap/kmap**：提供最小映射路径，vmalloc/ioremap 通过内核页表建立映射，kmap 对低端内存走高半区线性映射。

## 10. kobject/kref 最小实现

- **kref**：提供最小引用计数接口（init/get/put），统一对象生命周期入口。
- **kobject**：提供 name/父子关系指针与 release 回调，kref 负责引用计数，kobj_type 作为最小类型钩子。
- **kset**：用于聚合 kobject 列表，driver core 维护 devices/drivers 的最小集合。
- **sysfs 映射**：`/sys/devices` 通过 devices kset 枚举，保持最小 kobject 驱动输出路径。
- **sysfs 总线视图**：`/sys/bus/platform/{devices,drivers}` 基于 kset 列表输出。
- **driver bind/unbind**：`/sys/bus/platform/drivers/<drv>/{bind,unbind}` 支持最小手工绑定/解绑。
- **mem_init**：把可用页挂入 zone 的 free_area，正式启用 buddy 分配路径。
- **slub/kmalloc**：小对象缓存与大对象按页分配，形成内核通用动态分配入口。
