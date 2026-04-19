# Lite OS Directory Structure

## 文档定位
- 这份文档是 **当前代码树的目录地图**，用于回答“目录里现在有什么、职责怎么分”。
- 若需要看“运行时当前如何工作”，以 [QA.md](file:///data25/lidg/lite/Documentation/QA.md) 为准。
- 若需要看“Linux 2.6 对齐进度”，以 [Linux26-Subsystem-Alignment.md](file:///data25/lidg/lite/Documentation/Linux26-Subsystem-Alignment.md) 为准。

## 顶层目录

### `arch/`
- 体系结构相关代码，当前以 `x86` 为主。
- `arch/x86/boot/boot.s` 是 Multiboot 入口、早期页表和高半区跳转逻辑。
- `arch/x86/kernel/` 放置 GDT/IDT、中断入口、PIC/APIC 占位、PCI 平台初始化、链接脚本等。

### `init/`
- 内核初始化主线。
- `init/main.c` 提供 `start_kernel()`、`rest_init()`、`kernel_init()`。
- `init/initramfs.c` 负责解析 `cpio newc` 并把 initramfs 展开到当前根文件系统。

### `kernel/`
- 架构无关的核心机制。
- 当前主要包括：
  - 调度与任务管理：`sched.c`、`fork.c`、`exit.c`、`wait.c`
  - 信号与凭证：`signal.c`、`cred.c`
  - 系统调用与日志：`syscall.c`、`printk.c`、`panic.c`
  - 启动参数与时钟事件：`params.c`、`clockevents.c`、`time.c`

### `mm/`
- 内存管理子系统。
- 包含 bootmem、zone/buddy、分页、`mmap`、`vmalloc`、`slab`、`swap`、`vmscan`、`rmap`、`filemap` 等实现。

### `fs/`
- VFS 与具体文件系统。
- 通用层包括：`inode.c`、`dentry.c`、`namei.c`、`namespace.c`、`open.c`、`read_write.c`、`readdir.c`、`ioctl.c`、`exec.c`。
- 具体文件系统目前包括：`ramfs/`、`procfs/`、`sysfs/`、`devtmpfs/`、`minixfs/`。
- `buffer.c` 与 `block_dev.c` 提供块设备/VFS 之间的衔接层。

### `drivers/`
- 驱动与驱动核心，目录组织有意向 Linux `drivers/` 靠拢。
- 当前主要子目录：
  - `drivers/base/`：device / driver / bus / class / uevent / virtual device
  - `drivers/block/`：`ramdisk`
  - `drivers/clocksource/`：系统时钟源/定时器
  - `drivers/input/keyboard/`、`drivers/input/serio/`：AT 键盘、i8042、serio
  - `drivers/tty/`：TTY core、`n_tty`、串口子驱动
  - `drivers/video/console/`：console 路由与 console driver
  - `drivers/pci/`、`drivers/nvme/`、`drivers/scsi/`、`drivers/virtio/`：PCI/PCIe、NVMe、SCSI、virtio 相关实现
- 当前树里 **没有独立的 VGA console 驱动目录**；console 输出当前收敛到串口/console routing。

### `include/`
- 公共头文件。
- `include/linux/`：内核子系统头文件，尽量采用 Linux 术语命名。
- `include/asm/`：x86 相关头文件。

### `lib/`
- 内核内部通用库与基础数据结构。
- 当前包括：`bitmap.c`、`idr.c`、`kobject.c`、`kref.c`、`libc.c`、`parser.c`、`rbtree.c`、`vsprintf.c`。

### `usr/`
- 用户态程序与 initramfs 载荷内容。
- 包括 `init.elf`、shell、测试程序以及用户态运行时（`crt0.s`、`ulib.*`）。
- `smoke` 相关测试程序也在这里生成并打包进 initramfs。

### `linux2.6/`
- vendored Linux 2.6 代码快照，用作概念、命名和流程对照基准。
- 这不是 Lite 的构建输入，而是对齐参考源。

### `Documentation/`
- 活跃文档目录。
- `QA.md`、`device_driver_model.md`、`memory_layout.md` 属于当前实现说明。
- 路线图、矩阵、审计、问题日志类文档会单独标明其“规划/历史”属性。

### `out/`
- 构建输出目录。
- `make clean` 会清理这里生成的中间文件与镜像产物。
