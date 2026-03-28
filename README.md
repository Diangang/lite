# Lite Kernel

Lite 是一款用于学习和演示操作系统底层原理的极简 32 位 x86 内核。

## 项目目标
- 提供一个干净、可读性强、易于实验的操作系统基础框架。
- 展示从引导（Bootloader）到内核主循环（Kernel Main）的完整接管过程。
- 演示如何配置 GDT、IDT、PIC 以及如何编写基础的硬件驱动（如 PS/2 键盘）。
- 作为操作系统爱好者的入门跳板，后续可继续扩展内存管理、进程调度等高级特性。

## 当前特性
- **Multiboot 兼容**：可以通过 GRUB 或 QEMU 直接加载启动。
- **GDT & IDT**：实现了自定义的全局描述符表和中断描述符表，支持 Ring 3 与 TSS。
  - 任务切换时会更新 TSS 的 `esp0` 指向当前任务内核栈顶，用于用户态陷入内核时的安全栈切换。
- **异常处理**：捕获 CPU 的 0-31 号异常，防止内核静默崩溃（提供 Kernel Panic 机制）。
- **硬件中断**：配置了 8259A PIC，正确映射了 IRQ，解决了与 CPU 异常的冲突。
- **基础驱动与模块**：
  - VGA 文本模式输出（支持退格擦除和自动滚屏）。
  - 串口（COM1）日志输出（方便在无图形界面下调试）。
  - PS/2 键盘驱动。
  - **PIT 可编程定时器**（提供系统 Tick 和 uptime 支持）。
  - 调度以 Tick 驱动时间片递减，时间片耗尽触发一次抢占式切换。
  - **物理内存管理 (page_alloc/buddy)**：
    - Multiboot E820 内存地图解析与 bootmem 早期保留。
    - **Buddy 框架**：支持按页 (4KB) 及按 order 的物理页分配与释放，并挂接到 zone/free_area 与 zonelist。
  - **虚拟内存管理 (paging/pgtable)**：
    - **分页机制 (Paging)**：开启 x86 保护模式分页，设置 CR3 和 CR0 寄存器。
    - **恒等映射**：将物理内存前 128MB 映射到相同的虚拟地址，保证内核与低端内存可访问。
    - **缺页异常处理**：捕获 `#PF` (Interrupt 14)，支持最小按需映射（not-present 缺页自动分配并映射），并基于用户 VMA 范围校验合法性。
    - **映射查询**：提供虚拟地址是否已映射与虚实地址转换的辅助接口。
    - **独立页目录**：支持克隆内核页目录并在任务间切换。
    - **用户态回收**：用户进程退出后基于 VMA 回收用户页，并释放非内核共享的页表页与页目录页。
  - **vmalloc/ioremap/kmap 骨架**：预留高端映射与设备映射入口。
  - **内核对象分配 (SLUB 框架)**：
    - 实现了 `kmem_cache` 框架与 `kmalloc/kfree`，支持小对象缓存与大对象按页分配。
    - 初始化流程为 `bootmem → zones → build_all_zonelists → free_area_init → paging → mem_init → kswapd_init → slab`，为后续对齐 Linux 2.6 留出替换接口。
  - **极简标准 C 库**（实现了 `printf`, `memset`, `memcpy`, `itoa` 等核心函数）。
- **Initramfs (CPIO newc)**：
  - 通过 Multiboot module 加载 cpio 归档，并在内核早期解包到 rootfs（ramfs）。
  - 支持目录与常规文件的创建，形成 `/sbin/init`、`/bin/smoke` 等用户态镜像。
- **procfs（最小可观测接口）**：
  - `/proc/tasks`：任务列表与状态（`cat proc/tasks`）。
  - `/proc/sched`：tick 与上下文切换统计（`cat proc/sched`）。
  - `/proc/irq`：IRQ0/IRQ1/IRQ4 与 syscall 计数（`cat proc/irq`）。
  - `/proc/maps`：当前任务的 VMA 列表（`cat proc/maps`）。
  - `/proc/self/maps`：当前任务 VMA（更 Linux-like 的路径形式）。
  - `/proc/<pid>/maps`：指定 pid 的 VMA（例如 `cat proc/1/maps`）。
  - `/proc/meminfo`：物理内存总量、空闲量与 watermarks/kswapd 统计（`cat proc/meminfo`）。
  - `/proc/cow`：COW 缺页次数与复制次数统计（`cat proc/cow`）。
  - `/proc/mounts`：当前挂载表（`cat proc/mounts`）。
  - `/proc/<pid>/stat`：任务基础状态（`cat proc/1/stat`）。
  - `/proc/<pid>/cmdline`：任务名（`cat proc/1/cmdline`）。
  - `/proc/<pid>/status`：任务可读状态（含 Type/Cwd，`cat proc/1/status`）。
  - `/proc/<pid>/cwd`：任务 cwd（`cat proc/1/cwd`）。
  - `/proc/<pid>/fd/<n>`：fd 指向的节点名（`cat proc/1/fd/0`）。
- **devtmpfs（最小设备节点）**：
  - `/dev/console`：内核控制台输出通道（字符设备），用于内核日志与紧急输出落点。
  - `/dev/tty`：用户态终端 I/O 入口（字符设备），走 tty 行规程与回显/规范模式。
  - 设备节点由设备模型的注册结果驱动生成，行为更接近 Linux 的 devtmpfs。
- **sysfs（最小自描述接口）**：
  - `/sys/kernel/version`、`/sys/kernel/uptime`。
  - `/sys/devices/<dev>/{type,bus,driver}`：设备模型最小视图（目前默认注册 console 并自动绑定同名 driver）。
  - `/sys/bus/platform/{devices,drivers}`：基于 kset 的最小总线视图，drivers 支持 `bind/unbind`。
- **驱动模型与设备树**：
  - 引入类 Linux 2.6 的 `driver_init`、分级 initcall 段收集与 `module_init` 宏自动加载机制（分级条目最终被链接到连续的 initcall 段，内核通过 `__initcall_start..__initcall_end` 统一遍历；级别顺序由链接脚本中各 `.initcallN.init` 的排列保证）。
  - **初始化解耦**：将内核的“早期打印控制台（Early Console，无中断、轮询输出）”与“完整设备驱动（中断使能、队列管理）”彻底分离。核心初始化（CPU/内存/中断）在 `start_kernel` 中完成，而完整的驱动初始化被延迟到 `PID=1` 的内核 `init` 线程中，通过 `do_initcalls` 安全加载，完美符合 Linux 规范。
  - **kobject/kref 基础**：提供最小对象生命周期管理与引用计数，为 sysfs 与 driver core 提供一致化底座。
  - **kset/kobj_type 骨架**：提供最小聚合与类型钩子结构，为后续 sysfs 自动映射做准备。
- **用户态交互**：完全移除内核态 Shell，由 1号进程 (`/sbin/init`) 挂载文件系统并 fork 执行真正的用户态 Shell (`/sbin/sh`)，实现彻底的特权级分离。内置提供基于 C 语言编写的集成测试程序 `/bin/smoke`。
- **PID 1 对齐 Linux**：PID 1 在完成 initcall 与挂载后会直接“exec”为用户态 `/sbin/init`（不再额外创建一个新 pid），语义更接近 Linux 2.6 的 `kernel_init -> execve(init)`。
- **调度自测（用户态 smoke）**：调度相关的演示/自测已迁移到用户态 `/bin/smoke`，通过 `fork + sleep + yield` 验证 Tick 驱动的时间片递减、阻塞/让出与上下文切换路径。
- **文件系统 (VFS)**：结构体 (`i_ino`, `i_mode`, `i_size`) 和全局动态 inode 分配器 (`get_next_ino`) 完美对齐 Linux 2.6 标准，支持虚拟文件系统如 `ramfs`、`devtmpfs`、`procfs`、`sysfs`。
- **系统调用 (int 0x80)**：
  - 用户态 syscall 会进行用户指针校验，避免非法地址导致内核崩溃。
  - `SYS_WRITE/SYS_READ` 在内核侧通过 `copy_from_user/copy_to_user` 分段拷贝访问用户缓冲区。
  - syscall 分发器只负责寄存器参数解包与分发，具体的 `sys_read/sys_write/sys_open/...` 实现在对应子系统（`fs/*`、`kernel/exit.c` 等），组织方式对齐 Linux 2.6。
  - `SYS_READ/SYS_WRITE` 提供最小 `read(fd,...)` / `write(fd,...)` 风格接口（fd=0/1/2 绑定 `/dev/tty`），fdtable 为 per-task，fd 持有 file 对象与 offset。
  - `SYS_OPEN/SYS_CLOSE/SYS_UNLINK` 提供最小路径打开、关闭与删除文件能力（路径解析基于 VFS mount 表），支持内存物理页回收。
  - `SYS_CHDIR/SYS_GETCWD/SYS_GETDENTS/SYS_MKDIR` 支持用户态 shell 做路径切换、目录遍历与创建目录。
  - `SYS_GETUID/SYS_GETGID/SYS_UMASK/SYS_CHMOD` 提供最小权限与掩码接口。
  - `SYS_EXECVE` 支持在用户态替换当前进程映像（最小 exec）。
  - `SYS_WAITPID` 支持用户态等待子进程退出并获取退出信息。
  - `SYS_IOCTL` 提供最小设备控制入口（`/dev/console` 支持获取/设置 tty flags）。
  - `SYS_KILL` 提供最小信号投递入口（当前支持 SIGINT 中断前台任务）。
  - `SYS_MMAP/SYS_MUNMAP` 提供匿名映射与回收（缺页按 VMA 规则处理，`/proc/<pid>/maps` 可观测）。
  - 缺页处理支持将 VMA 允许的 supervisor 映射修正为用户页。
  - 低端恒等映射区域的用户访问已通过缺页修正兼容（避免 present fault）。
  - `SYS_FORK` 提供最小 fork，与 COW 页引用计数配合实现写时复制。
  - `SYS_BRK` 提供最小用户堆扩展接口（基于堆 VMA 与按需缺页分配）。
  - syscall 入口使用 trap gate，不会隐式关闭中断，内核态具备可抢占的基础语义。
- **用户态异常处理**：
  - 用户态触发 `#PF/#GP/#UD` 等异常时，内核终止当前用户任务并继续运行 shell。

## 构建与运行

### 依赖环境
- Linux 系统（推荐 Ubuntu/Debian）
- `gcc` 和 `binutils`（用于编译 32 位代码）
- `qemu-system-i386`（用于运行和调试）
- `xorriso` 和 `mtools`（仅当需要打包为 ISO 镜像时需要）

### 编译与启动
在终端中进入 `lite` 目录，执行以下命令：

```bash
# 清理并编译内核二进制与 initramfs
make clean && make

# 使用 QEMU 启动内核（图形界面模式）
# 注意：此模式下 Shell 输入依赖 QEMU 窗口焦点
qemu-system-i386 -kernel out/myos.bin -initrd out/initramfs.cpio -m 512M

# [推荐] 使用无头模式 + 串口交互启动
qemu-system-i386 -kernel out/myos.bin -initrd out/initramfs.cpio -m 512M -serial stdio -display none
```

*提示：启动后，您可以在当前终端直接与 OS Shell 交互。支持直接输入路径（如 `/bin/smoke`）执行用户态程序。按 `Ctrl+A` 然后按 `X` 退出 QEMU。*

## 头文件布局（开发约定）
- `include/linux/`：核心子系统头（`sched/mm/fs/file/fdtable/syscall/...`），尽量按职责拆分，避免聚合大头文件。
- `include/asm/`：x86 相关头（`ptrace/processor/irqflags/unistd/multiboot/gdt/idt` 等）。
- 工程不再使用 `include/*.h` 的“扁平头文件”，代码中统一 `#include "linux/xxx.h"` 或 `#include "asm/xxx.h"`。

## 学习指南
如果您想深入了解本项目每一行代码的运作原理，请阅读 [Documentation/Annotation.md](./Documentation/Annotation.md)。该文档详细拆解了引导流程、中断上下文切换、驱动模型、VFS、系统调用等核心细节。

此外，在 `Documentation/` 目录下还提供了以下参考资料：
- [Issues.md](./Documentation/Issues.md)：项目开发过程中的疑难 Bug 及排查过程汇总（包含 InitRD 和串口交互等经典问题）。
- [Directory-Structure.md](./Documentation/Directory-Structure.md)：详细的目录结构与模块职责说明。
