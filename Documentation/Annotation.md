# Lite OS Annotation（按当前代码结构梳理）


## 文档定位
- 这是一份**源码导读/总览文档**，用于帮助建立当前系统主线与模块边界的整体地图。
- 它强调结构理解，不追求逐符号、逐函数精确覆盖；具体接口与行为请回到对应源码与 `QA.md`。

本文档不是逐文件逐行注释，而是站在“当前系统主链路如何拼起来”的角度，对代码目录、模块边界和关键入口进行归纳。适合在阅读源码前先建立整体地图。

---

## 1. 总体结构

当前 Lite OS 已经具备一条完整的最小操作系统主线：
- x86 32 位高半区内核启动
- Multiboot + initramfs
- bootmem + zone + buddy + paging
- 任务/调度/用户态执行
- VFS + ramfs/procfs/sysfs/devtmpfs/minixfs
- 设备模型 + PCI + NVMe 测试链路
- 块层 + buffer cache + page cache + 最小 writeback/reclaim

从目录上可以粗略分成：
- [arch/x86](file:///data25/lidg/lite/arch/x86)：体系结构相关代码，包含启动汇编、GDT/IDT/PIC、分页入口等
- [init](file:///data25/lidg/lite/init)：内核启动主线、initramfs 解包、内核初始化流程
- [kernel](file:///data25/lidg/lite/kernel)：任务、调度、系统调用、信号、等待队列等核心机制
- [mm](file:///data25/lidg/lite/mm)：bootmem、zone、页分配、缺页、VMA、swap、slab、page cache
- [fs](file:///data25/lidg/lite/fs)：VFS 与各具体文件系统实现
- [drivers](file:///data25/lidg/lite/drivers)：设备模型、总线、TTY、时钟、PCI、NVMe、块设备等
- [usr](file:///data25/lidg/lite/usr)：用户态程序，当前最重要的是综合回归测试 [smoke.c](file:///data25/lidg/lite/usr/smoke.c)

---

## 2. 启动主线

### 2.1 入口汇编：`arch/x86/boot/boot.s`

这是内核第一条执行路径，负责三件事：
- 声明 Multiboot 头，让 bootloader 愿意加载它
- 建立最早期的 GDT、栈和页表
- 打开分页并跳转到高半区，再进入 C 世界

当前早期页表策略是：
- 先做 **低端 4MB 恒等映射**
- 再做 **高半区动态范围映射**

这里的“高半区”指的是：**把内核放到 32 位虚拟地址空间的高地址部分运行**。当前项目采用经典的 `3G/1G split` 思路，即：
- `0x00000000 ~ 0xBFFFFFFF`：主要留给用户态
- `0xC0000000 ~ 0xFFFFFFFF`：留给内核

因此这里的“高半区映射”实际是指：
- 把**物理低端的一段连续内存**
- 映射到从 `0xC0000000` 开始的内核虚拟地址
- 起点页目录项是 `PDE[768]`，因为 `0xC0000000 >> 22 = 768`
- 映射长度不是固定写死的 128MB，而是按 `max(end, mmap_end, mods_end) + INIT_MAP_BEYOND_END` 动态向上取整得到
- 这也意味着：**物理低端前 4MB 最终同样被映射进了高半区**

更具体地说，在启动过渡阶段，同一批“物理低端前 4MB”会同时拥有两套虚拟地址别名：
- 低地址别名：`0x00000000 ~ 0x003FFFFF` -> 物理 `0x00000000 ~ 0x003FFFFF`
- 高半区别名：`0xC0000000 ~ 0xC03FFFFF` -> 物理 `0x00000000 ~ 0x003FFFFF`

也就是说，被“删掉”的只是**低地址那份恒等映射**，不是“物理低端 4MB 本身不再可访问”。当跳转到 `trampoline_high` 后，代码会清空 `PDE[0]`，再重新写 CR3 刷新 TLB（见 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L132-L138)）。从那一刻起：
- 低地址 `0x00000000 ~ 0x003FFFFF` 不再保留给内核继续执行
- 但同一批物理页仍然能通过高半区 `0xC0000000 ~ 0xC03FFFFF` 访问

所以从“物理内存最终怎么被内核看见”的角度说：
- **物理低端 4MB 最终仍然在高半区内核线性映射范围里**
- 启动阶段额外保留的低地址恒等映射，只是为了完成“开分页 -> 跳高地址”的过渡

而“低端 4MB 恒等映射”的作用是：
- 打开分页的瞬间，CPU 仍然在按原来的低地址取指执行
- 如果此时没有 `virt == phys` 的临时映射，刚打开 `CR0.PG` 就会立刻 fault
- 所以要先保留一小段“过渡跑道”，等跳到高半区入口后，再把 `PDE[0]` 清掉（见 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L90-L138)）

这里的 **EIP** 可以直接理解成：
- **CPU 当前/下一条要执行指令的位置寄存器**
- 在 32 位 x86 下它叫 **EIP（Extended Instruction Pointer）**
- 打开分页前，EIP 里的地址基本按“线性地址≈物理地址”去解释
- 打开分页后，EIP 里的值会被当成**虚拟地址**去查页表

所以“刚开分页还能继续跑”的关键就在于：
- 开分页那一瞬间，EIP 里通常还是原来那个低地址
- 因为 `PDE[0]` 提供了恒等映射，所以这个低地址现在被当作低虚拟地址后，仍然能映射回原来的低物理地址
- 紧接着 `jmp *%eax` 把 EIP 显式改成 `KERNEL_BASE + trampoline_high`，也就是**高半区虚拟地址**
- 从那之后，CPU 继续取指时就通过高半区映射访问同一份内核代码

当前项目的早期策略可以概括为：
- **低端 4MB 恒等映射**：保证“刚开分页时还能继续跑”
- **高半区动态映射**：保证“跳到内核高地址后，内核代码、启动数据和 multiboot 相关结构都可访问”

Linux 2.6 的 32 位 x86 主线思想和这里是一样的，也是**高半区内核**。它同样会在 `startup_32` 里同时建立：
- 一份低地址恒等映射
- 一份从 `__PAGE_OFFSET` 开始的内核高地址映射

可参考 Linux 2.6 的 [head.S](file:///data25/lidg/bsk/arch/i386/kernel/head.S#L81-L111)：
- 它把同一批早期页表同时挂到 `virtual 0` 和 `PAGE_OFFSET`
- 然后在 [head.S:L186-L191](file:///data25/lidg/bsk/arch/i386/kernel/head.S#L186-L191) 装入 `swapper_pg_dir` 并打开分页

两者的差别主要不在“有没有高半区”，而在**早期映射覆盖范围和后续扩展策略**：
- 当前 Lite OS 现在也已经改成**按需要动态扩展高半区映射范围**，不再固定写死 128MB；不过它还要额外覆盖 Multiboot 的 `mmap`、`module` 结构和模块 payload 末尾，便于在进入 C 之前就能稳定访问这些启动数据
- Linux 2.6 会根据 `_end + 页表自身空间 + INIT_MAP_BEYOND_END` 动态扩展早期映射范围（见 [head.S:L81-L111](file:///data25/lidg/bsk/arch/i386/kernel/head.S#L81-L111)），语义上和当前实现已经更接近
- Linux 后续还会在真正的 `paging_init()` 中建立更完整的 lowmem 线性映射、fixmap、pkmap、vmalloc 等布局；而当前项目的高半区布局仍更偏“最小可运行版本”

所以“早期页表策略”这个词，指的就是：
- **在真正完整内存管理初始化前**
- 先用一组最小页表
- 同时解决“打开分页不能死”和“内核要搬到高地址运行”这两个问题

这样打开分页的瞬间不会 triple fault，同时 C 内核可以直接在高地址运行。关键代码在 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L57-L138)。

### 2.2 C 入口：`init/main.c`

[start_kernel](file:///data25/lidg/lite/init/main.c#L58-L119) 是当前系统的主入口。初始化顺序可以概括为：
- 串口
- `setup_arch`
- bootmem / zone / buddy / paging
- `mem_init`
- `kswapd_init` / `swap_init`
- `kmem_cache_init`
- `sched_init` / `fork_init`
- 命令行
- `init_timer`
- `rest_init`

后续：
- `rest_init()` 生成 `kernel_init` 线程（PID 1）并让当前线程变成 idle（PID 0）
- `kernel_init()` 里完成驱动核心、initcall、syscall、VFS、namespace 和用户态 init 启动

因此 [main.c](file:///data25/lidg/lite/init/main.c) 就是阅读整个系统时最值得先看的文件。

### 2.3 initramfs：`init/initramfs.c`

当前根文件系统不是从块设备直接引导，而是：
- bootloader 把 CPIO 镜像作为 Multiboot module 读入内存
- 内核先挂载 ramfs 为 `/`
- 再由 [populate_rootfs](file:///data25/lidg/lite/init/initramfs.c#L44-L105) 把 CPIO 内容解包到 `/`

这意味着当前用户态程序、脚本和基础目录结构，初始都来自 initramfs。

---

## 3. 体系结构与中断

### 3.1 GDT / TSS

相关代码主要在：
- [gdt.c](file:///data25/lidg/lite/arch/x86/kernel/gdt.c)
- [gdt_flush.s](file:///data25/lidg/lite/arch/x86/kernel/gdt_flush.s)

当前 GDT 的作用不是“复杂分段管理”，而是提供：
- 内核态代码/数据段
- 用户态代码/数据段
- TSS 描述符

TSS 主要用于用户态陷入内核时的栈切换，而不是使用 x86 的“硬件任务切换”。

### 3.2 IDT / ISR / IRQ / PIC

相关文件：
- [idt.c](file:///data25/lidg/lite/arch/x86/kernel/idt.c)
- [interrupt.s](file:///data25/lidg/lite/arch/x86/kernel/interrupt.s)
- [irq.c](file:///data25/lidg/lite/arch/x86/kernel/irq.c)
- [isr.c](file:///data25/lidg/lite/arch/x86/kernel/isr.c)

当前中断体系的结构是：
- 汇编 stub 负责保存现场与构造 `pt_regs`
- C 层 `isr_handler/irq_handler` 负责统一分发
- PIC 会被 remap 到 `0x20 ~ 0x2F`

这套实现已经足够支撑：
- 定时器中断
- 键盘输入
- 串口中断
- 页故障 / 常规 CPU 异常

### 3.3 时钟与调度时基

PIT 初始化在 [timer.c](file:///data25/lidg/lite/drivers/clocksource/timer.c)，核心作用是：
- 周期性产生 IRQ0
- 推动 `jiffies/tick`
- 作为 sleep、调度和超时的基本时基

---

## 4. 内存管理

### 4.1 bootmem：早期物理内存识别

核心文件是 [bootmem.c](file:///data25/lidg/lite/mm/bootmem.c)。

它负责：
- 解析 Multiboot memory map
- 构建内部 `e820` 风格表
- 记录 kernel image / module / reserved 区间
- 提供早期 `bootmem_alloc()`

bootmem 阶段的目标不是“高效分配”，而是：
- 在真正的页分配器 ready 之前，先给内核元数据、页表、zone 结构找安全存放位置

### 4.2 zone + buddy：主物理页分配器

核心文件：
- [mmzone.c](file:///data25/lidg/lite/mm/mmzone.c)
- [page_alloc.c](file:///data25/lidg/lite/mm/page_alloc.c)
- [mmzone.h](file:///data25/lidg/lite/include/linux/mmzone.h)

当前已经是最小可用的 zoned buddy：
- `ZONE_DMA`
- `ZONE_NORMAL`
- `MAX_ORDER` 多阶空闲块
- `watermark[min/low/high]`
- zonelist fallback

但还没有：
- `ZONE_HIGHMEM`
- per-cpu page list
- NUMA / cpuset / migratetype

所以它更接近“Linux 2.6 风格骨架已搭起来，但策略层还很简化”。

### 4.3 paging / page fault / COW

核心文件是 [memory.c](file:///data25/lidg/lite/mm/memory.c)。

主要职责：
- 建立内核页表模板
- 切换页目录 / 写 CR3
- 提供 `map_page_ex()`、`virt_to_phys()` 等页表接口
- 处理 page fault
- 处理 COW fault

当前缺页处理已经覆盖：
- not-present 匿名页按需分配
- VMA 权限校验
- present fault 上的 COW resolve
- 简化 swap-in

因此当前 MM 已经不只是“静态映射内核”，而是具备了用户态地址空间与最小按需分页能力。

### 4.4 VMA、`mmap`、`brk`

核心文件是 [mmap.c](file:///data25/lidg/lite/mm/mmap.c)。

当前实现特点：
- `mm->mmap` 仍是线性链表
- `mmap/brk/mprotect/mremap` 都已经可用
- VMA 是缺页处理与用户地址合法性的核心依据

这部分已经构成“用户地址空间管理”的主干，但还没有 Linux 2.6 那种：
- rbtree
- mmap_cache
- 复杂 VMA merge/split 策略

### 4.5 rmap / reclaim / swap

核心文件：
- [rmap.c](file:///data25/lidg/lite/mm/rmap.c)
- [vmscan.c](file:///data25/lidg/lite/mm/vmscan.c)
- [swap.c](file:///data25/lidg/lite/mm/swap.c)

当前特点是：
- rmap 已足够支撑最小 COW 和换出判断
- `vmscan` 已有基础的 cache reclaim + 换出触发
- `swap` 是固定 slot、内存缓冲型的最小实现

因此这条线已经形成“可测试闭环”，但仍不是 Linux 2.6 的完整 LRU reclaim 体系。

### 4.6 slab / page cache

核心文件：
- [slab.c](file:///data25/lidg/lite/mm/slab.c)
- [filemap.c](file:///data25/lidg/lite/mm/filemap.c)
- [pagemap.h](file:///data25/lidg/lite/include/linux/pagemap.h)

当前：
- slab 提供通用内核对象分配
- page cache 提供 `address_space + page_cache_entry`
- 已有最小 writeback 统计和同步 flush

但离 Linux 2.6 还差：
- radix-tree/xarray 风格的高效索引
- 完整 dirty throttling
- 背景 writeback 线程

---

## 5. 任务、调度与系统调用

### 5.1 任务与调度

核心文件：
- [sched.c](file:///data25/lidg/lite/kernel/sched.c)
- [fork.c](file:///data25/lidg/lite/kernel/fork.c)
- [exit.c](file:///data25/lidg/lite/kernel/exit.c)
- [wait.c](file:///data25/lidg/lite/kernel/wait.c)

当前已经支持：
- `fork`
- `waitpid`
- `exit`
- `sleep`
- `yield`
- 内核线程与用户线程并存

从 smoke 测试看，这一套已经能支撑：
- 父子进程语义
- zombie 回收
- COW
- 前后台简单调度

### 5.2 系统调用

核心文件：
- [syscall.c](file:///data25/lidg/lite/kernel/syscall.c)
- [syscall_entry.s](file:///data25/lidg/lite/arch/x86/kernel/syscall_entry.s)

当前 syscalls 已覆盖：
- 进程控制
- 文件 I/O
- 内存管理
- 信号相关
- 基础设备与 tty 访问

用户态程序并不是“直接调用内核函数”，而是通过系统调用入口进入 Ring 0。

### 5.3 信号与前台终端

相关文件：
- [signal.c](file:///data25/lidg/lite/kernel/signal.c)
- [tty_io.c](file:///data25/lidg/lite/drivers/tty/tty_io.c)

当前已经实现了：
- `SIGCHLD`
- `SIGTERM`
- `SIGKILL`
- `SIGINT`
- 前台终端与 `Ctrl-C`

这使得当前用户态不只是“能跑程序”，而是具备了最小交互式进程控制能力。

---

## 6. 文件系统与命名空间

### 6.1 VFS 核心

核心文件：
- [namespace.c](file:///data25/lidg/lite/fs/namespace.c)
- [namei.c](file:///data25/lidg/lite/fs/namei.c)
- [open.c](file:///data25/lidg/lite/fs/open.c)
- [read_write.c](file:///data25/lidg/lite/fs/read_write.c)
- [super.c](file:///data25/lidg/lite/fs/super.c)

当前 VFS 已经具备：
- inode / dentry / file / super_block 基本模型
- 路径解析
- mount table
- 打开、读写、创建、删除、目录操作

这意味着上层文件系统已经能走统一接口，而不是每个文件系统直接暴露私有 API。

### 6.2 当前文件系统家族

当前主要包括：
- [ramfs](file:///data25/lidg/lite/fs/ramfs)：根文件系统
- [procfs](file:///data25/lidg/lite/fs/procfs/procfs.c)：内核状态导出
- [sysfs](file:///data25/lidg/lite/fs/sysfs/sysfs.c)：设备模型可视化
- [devtmpfs](file:///data25/lidg/lite/drivers/base/devtmpfs.c)：driver core 维护的 `/dev` 节点，底层承载使用 ramfs
- [minixfs](file:///data25/lidg/lite/fs/minixfs/minixfs.c)：真实块设备文件系统测试目标

其中：
- `/` 是 ramfs
- `/proc` 是 procfs
- `/sys` 是 sysfs
- `/dev` 是 devtmpfs
- `/mnt` 当前通常挂的是 `ram1` 上的 MinixFS

### 6.3 procfs 的角色

[procfs.c](file:///data25/lidg/lite/fs/procfs/procfs.c) 现在已经不只是“示例文件系统”，而是当前调试与观测的重要窗口。

已导出的典型内容包括：
- `/proc/meminfo`
- `/proc/iomem`
- `/proc/maps`
- `/proc/mounts`
- `/proc/cow`
- `/proc/pfault`
- `/proc/vmscan`
- `/proc/writeback`
- `/proc/pagecache`
- `/proc/blockstats`
- `/proc/diskstats`

很多 smoke 测试就是围绕这些接口做验证。

---

## 7. 控制台、TTY 与用户交互

### 7.1 console / tty / serial 的边界

核心文件：
- [printk.c](file:///data25/lidg/lite/kernel/printk.c)
- [tty_io.c](file:///data25/lidg/lite/drivers/tty/tty_io.c)
- [8250.c](file:///data25/lidg/lite/drivers/tty/serial/8250.c)

可以这样理解：
- `serial`：具体硬件端
- `console`：内核日志输出抽象（当前只分发到 serial）
- `tty`：终端输入输出与行规约抽象（当前只输出到 serial）

当前实现的一个重要特点是：
- **当前只保留串口作为输出通道**，VGA 相关代码已移除，后续会按 Linux 2.6 风格重做 VGA/vt 支持。

VGA 参考（保留用于后续重做）：
- VGA 文本模式常见做法是把屏幕字符缓冲映射到物理地址 `0xB8000`，每个字符 2 字节（ASCII + 属性）
- 这种方式不等同于“完整显卡驱动”，更像是启动阶段 BIOS/UEFI 已经把显卡置于兼容文本模式，内核只需往显存窗口写字
- 详细解释与历史实现背景见归档文档 [Kernel-QA.md:L513-L599](file:///data25/lidg/lite/Documentation/archived/Kernel-QA.md#L513-L599)

### 7.2 键盘与终端输入

相关文件：
- [keyboard.c](file:///data25/lidg/lite/drivers/input/keyboard.c)
- [tty_io.c](file:///data25/lidg/lite/drivers/tty/tty_io.c)

主线是：
- 键盘 IRQ
- 读 PS/2 扫描码
- 转成字符
- 写入 TTY 输入缓冲
- 唤醒阻塞读者

所以当前键盘输入已经接入到了真正的用户态终端语义中，而不是只做屏幕回显。

---

## 8. 设备模型与驱动

### 8.1 driver core

核心文件：
- [drivers/base/init.c](file:///data25/lidg/lite/drivers/base/init.c)
- [drivers/base/core.c](file:///data25/lidg/lite/drivers/base/core.c)

当前 driver core 已经建立了：
- `platform` bus
- `console` class
- `tty` class
- `devices/drivers/classes` 三组 kset
- 设备注册 / 驱动注册 / 匹配绑定

因此当前不是“驱动自己硬编码创建设备节点”，而是已经进入了最小设备模型阶段。

### 8.2 PCI 与 sysfs

相关文件：
- [pci.c](file:///data25/lidg/lite/drivers/pci/pci.c)
- [sysfs.c](file:///data25/lidg/lite/fs/sysfs/sysfs.c)
- [ksysfs.c](file:///data25/lidg/lite/kernel/ksysfs.c)

当前已经支持：
- 扫描 PCI bus 0
- 读取配置空间
- 创建 `/sys/devices` / `/sys/bus/pci`
- 生成部分 uevent 文本
- 基础 bind/unbind 验证

这使得 PCI 已不只是“裸读配置空间”，而是进入了“可观测、可绑定”的设备模型框架。

### 8.3 NVMe 的当前实现定位

[nvme.c](file:///data25/lidg/lite/drivers/nvme/nvme.c) 当前的定位不是完整 NVMe 协议栈，而是：
- 验证 PCIe 设备枚举
- BAR 映射
- 通过设备模型注册 namespace
- 打通到块设备和 `/dev/nvme0n1`

当前特点：
- namespace 大小固定为测试用的 16MB
- 没有完整 admin/io queue 协议收发
- 更接近“块设备链路与设备模型贯通测试”

---

## 9. 块层与缓存层

### 9.1 块设备抽象

核心文件：
- [blkdev.c](file:///data25/lidg/lite/drivers/block/blkdev.c)
- [bio.c](file:///data25/lidg/lite/block/bio.c)
- [blk_queue.c](file:///data25/lidg/lite/block/blk_queue.c)
- [blk_request.c](file:///data25/lidg/lite/block/blk_request.c)

当前已经形成了一个最小块层骨架：
- `bio`
- `request`
- `request_queue`
- `submit_bio()`
- `request_fn`

对内存块设备来说，最终就是把数据拷进 `bdev->data`；但接口层次已经和 Linux 块层语义接近。

### 9.2 buffer cache

核心文件是 [buffer.c](file:///data25/lidg/lite/fs/buffer.c)。

当前 buffer cache 的特点：
- 以 `(bdev, blocknr, size)` 做 hash
- 提供 `bread()` / `brelse()`
- 提供 `mark_buffer_dirty()` / `sync_dirty_buffer()`
- 有全局上限和简单逐项淘汰

它主要服务于：
- MinixFS 这类基于块的元数据与目录项访问

### 9.3 page cache / writeback

核心文件是 [filemap.c](file:///data25/lidg/lite/mm/filemap.c)。

当前 page cache 已具备：
- `address_space`
- 文件页缓存项
- dirty 计数
- 同步 `writeback_flush_all()`
- 单页回收

但还没有 Linux 2.6 风格的：
- file-backed fault 深度整合
- readahead
- 背景 writeback 线程
- 脏页节流

---

## 10. 用户态程序与测试

### 10.1 用户态加载

核心文件：
- [exec.c](file:///data25/lidg/lite/fs/exec.c)
- [binfmts.h](file:///data25/lidg/lite/include/linux/binfmts.h)

当前 ELF 加载路径已经支持：
- 建立新 `mm`
- 为 `PT_LOAD` 建 VMA
- 分配用户页并拷贝段内容
- 建立用户栈
- 切换到 Ring 3 执行

这让系统具备了真正“内核加载 ELF 用户程序”的能力。

### 10.2 `usr/smoke.c`

[smoke.c](file:///data25/lidg/lite/usr/smoke.c) 现在实际上承担了“系统级回归测试入口”的角色，覆盖了：
- `fork/waitpid`
- 文件 I/O
- page fault / COW
- `mmap/mprotect/mremap`
- scheduler
- `/proc` / `/sys` / `/dev`
- writeback / page cache / vmscan
- MinixFS 读写
- NVMe 设备存在性

因此如果想快速判断某条主线是否打通，`smoke` 是最有价值的用户态入口。

---

## 11. 阅读建议

如果第一次读这个项目，建议按下面顺序：
- 启动主线：[boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s) -> [main.c](file:///data25/lidg/lite/init/main.c)
- 内存主线：[bootmem.c](file:///data25/lidg/lite/mm/bootmem.c) -> [mmzone.c](file:///data25/lidg/lite/mm/mmzone.c) -> [page_alloc.c](file:///data25/lidg/lite/mm/page_alloc.c) -> [memory.c](file:///data25/lidg/lite/mm/memory.c)
- 任务主线：[sched.c](file:///data25/lidg/lite/kernel/sched.c) -> [fork.c](file:///data25/lidg/lite/kernel/fork.c) -> [exec.c](file:///data25/lidg/lite/fs/exec.c)
- 文件系统主线：[namespace.c](file:///data25/lidg/lite/fs/namespace.c) -> [namei.c](file:///data25/lidg/lite/fs/namei.c) -> [open.c](file:///data25/lidg/lite/fs/open.c) -> [minixfs.c](file:///data25/lidg/lite/fs/minixfs/minixfs.c)
- 设备主线：[drivers/base/init.c](file:///data25/lidg/lite/drivers/base/init.c) -> [core.c](file:///data25/lidg/lite/drivers/base/core.c) -> [pci.c](file:///data25/lidg/lite/drivers/pci/pci.c) -> [nvme.c](file:///data25/lidg/lite/drivers/nvme/nvme.c)
- I/O 交互主线：[tty_io.c](file:///data25/lidg/lite/drivers/tty/tty_io.c) -> [8250.c](file:///data25/lidg/lite/drivers/tty/serial/8250.c)

如果只是想验证系统现状，可以直接看：
- [QA.md](file:///data25/lidg/lite/Documentation/QA.md)
- [smoke.c](file:///data25/lidg/lite/usr/smoke.c)

---

## 12. 当前定位

当前代码最适合这样理解：
- 它已经不是“只有 boot demo 的练习代码”
- 也还不是完整 Linux 2.6 级别的生产型内核
- 它处在“**关键主链路已经真实打通、便于继续向 Linux 2.6 语义靠拢**”的阶段

因此这份 Annotation 的价值，不在于替代源码，而在于先告诉你：
- 哪些模块已经是当前主线
- 哪些实现是最小闭环
- 哪些部分仍是后续 roadmap 的重点改造对象
