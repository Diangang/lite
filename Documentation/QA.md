# Lite OS QA（按当前实现梳理）

本文基于当前仓库代码整理，目标是回答“这套系统现在到底怎么工作”，而不是泛泛介绍 Linux 概念。回答优先对应当前实现，再在必要时说明与标准 Linux 2.6 的关系与差距。

---

## 1. 启动与平台

### Q1.1: 从上电到 PID 1，当前 Lite OS 的启动主线是什么？
**回答**：
1.  QEMU/bootloader 先按 Multiboot 规范把内核加载到物理内存，并把 `magic + multiboot_info` 传给入口汇编（见 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L29-L136)）。
2.  入口汇编建立早期页表：低端 4MB 恒等映射 + 高半区按需映射（覆盖内核末尾与 multiboot 启动数据），打开分页后跳转到高地址执行（见 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L69-L260)）。
3.  C 入口 `start_kernel()` 依次初始化串口、bootmem、zone、页分配器、分页、swap、slab、调度器与中断时钟（见 [main.c](file:///data25/lidg/lite/init/main.c#L115-L179)）。
4.  `rest_init()` 创建 `kernel_init` 线程作为 PID 1，自身变为 idle 线程 PID 0（见 [main.c](file:///data25/lidg/lite/init/main.c#L39-L54)）。
5.  `kernel_init()` 里做 `driver_init()`、执行 initcall、安装 syscall、建立 VFS/根文件系统并尝试执行 `/init` 或 fallback init（见 [main.c](file:///data25/lidg/lite/init/main.c#L121-L179)）。

### Q1.2: 当前项目里的内存地图是链接时确定的吗？
**回答**：
不是。链接脚本只决定**内核镜像自己的布局**，例如高半区虚拟基址 `0xC0000000`、物理装载基址 `0x00100000`，以及 `end` 符号的位置（见 [linker.ld](file:///data25/lidg/lite/arch/x86/kernel/linker.ld#L8-L83)）。

真正的物理内存地图来自启动时传入的 `multiboot_info.mmap_addr/mmap_length`。当前实现里：
- `bootmem_init()` 遍历 `multiboot_memory_map`，构建内部 `bootmem.e820[]`、`available[]`、`reserved[]`（见 [bootmem.c](file:///data25/lidg/lite/mm/bootmem.c#L164-L233)）。
- 后续 `mem_init()` 再依据这份 memory map 释放可用页到伙伴系统（见 [page_alloc.c](file:///data25/lidg/lite/mm/page_alloc.c#L404-L459)）。

所以：
- **链接脚本决定内核自身放哪儿**
- **启动时 memory map 决定整台机器哪些物理地址可分配**

### Q1.3: QEMU 的 `-machine q35` 会把 E820 “定死”吗？
**回答**：
不会由 `-machine q35` 单独决定，但它会显著影响结果。

更准确地说，当前实验环境里的 E820 风格内存地图由以下因素共同塑造：
- `-machine q35`：决定平台/芯片组风格，影响 PCIe/MMIO 洞与保留区布局。
- `-m 512M` 这类参数：决定 RAM 总量。
- 外挂设备：可能改变 MMIO 窗口分布。
- bootloader：把这份机器布局整理成 Multiboot memory map 再交给内核。

因此可以把链路理解为：
- **QEMU 决定底层物理地址空间布局**
- **Multiboot 把布局交给内核**
- **内核在 [bootmem_init](file:///data25/lidg/lite/mm/bootmem.c#L164-L233) 中解析**

### Q1.4: 当前 initramfs 是怎么进入内核并解包的？
**回答**：
1.  bootloader 把 `-initrd` 指定的 CPIO 镜像作为 Multiboot module 放进内存。
2.  `bootmem_init()` 会先把 module 占用的物理区间记为 reserved，避免早期分配器覆盖（见 [bootmem.c](file:///data25/lidg/lite/mm/bootmem.c#L173-L189)）。
3.  `prepare_namespace()` 调用 `populate_rootfs()`，它从 `boot_mbi.mods_addr -> mod_start/mod_end` 取出首个 module，并按 `cpio newc` 格式遍历条目（见 [main.c](file:///data25/lidg/lite/init/main.c#L146-L166)、[initramfs.c](file:///data25/lidg/lite/init/initramfs.c#L44-L105)）。
4.  目录条目会调用 `vfs_mkdir()`，普通文件会通过 `vfs_open + vfs_write` 写入当前根文件系统，也就是 ramfs（见 [initramfs.c](file:///data25/lidg/lite/init/initramfs.c#L80-L98)、[namespace.c](file:///data25/lidg/lite/fs/namespace.c#L177-L187)）。

### Q1.5: 当前 `initcall` 真正起作用了吗？
**回答**：
起作用了，但实现是最小版。

- `include/linux/init.h` 通过不同 section 把 `early_initcall/core_initcall/subsys_initcall/...` 放到 `.initcall*.init`（见 [init.h](file:///data25/lidg/lite/include/linux/init.h#L1-L20)）。
- 链接脚本把这些 section 聚合为 `__initcall_start ~ __initcall_end`（见 [linker.ld](file:///data25/lidg/lite/arch/x86/kernel/linker.ld#L38-L52)）。
- `do_initcalls()` 顺序遍历并执行（见 [main.c](file:///data25/lidg/lite/init/main.c#L121-L125)）。

当前像 PCI、pcie、ramdisk、serial、NVMe 等模块，都是通过 `subsys_initcall/module_init` 接入。

---

## 2. x86 保护模式与中断

### Q2.1: Ring 0 / Ring 3 是什么？为什么叫 Ring，而不是直接叫 privilege？
**回答**：
- 正式概念叫 **privilege level**，例如 CPL/DPL/RPL。
- “Ring” 是工程上的形象说法，因为 x86 保护模型常被画成**同心圆保护圈**：越靠中心权限越高。
- 因此：
  - **Ring 0**：最高权限，内核态
  - **Ring 3**：最低权限，用户态

当前项目的核心运行模型就是：
- 内核代码运行在 Ring 0
- 用户程序运行在 Ring 3
- 用户态进入内核依赖中断/系统调用入口

### Q2.2: GDT 在当前系统里到底承担什么角色？
**回答**：
当前实现仍然保留 x86 传统的 GDT，用它完成：
- 进入保护模式后的代码段/数据段装载
- 用户态与内核态段的 DPL 区分
- TSS 装载所需的系统段入口

但它**不是**当前地址空间隔离的核心。真正的隔离主要由页表和 Ring 权限完成。GDT 更像是：
- “让 CPU 能在保护模式里正确解释段”
- “为用户/内核与 TSS 提供最小必要描述符”

可参考 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L43-L55)、[gdt.c](file:///data25/lidg/lite/arch/x86/kernel/gdt.c)。

### Q2.3: 中断、中断号、IRQ 编号分别是什么？
**回答**：
- **中断**：总称，表示“当前执行流被打断去处理事件”。
- **IRQ 编号**：中断控制器侧的外部硬件请求线编号，例如传统 PIC 的 IRQ0~IRQ15。
- **中断号 / vector**：CPU 看到的 IDT 向量号，用它索引 IDT 项。

在当前实现中，PIC 被重新映射后：
- IRQ0 -> 0x20
- IRQ1 -> 0x21
- ...
- IRQ7 -> 0x27

定义可见 [interrupt.h](file:///data25/lidg/lite/include/linux/interrupt.h#L16-L27)。

### Q2.4: 为什么要 remap PIC？
**回答**：
因为 x86 的 0~31 号向量保留给 CPU 异常。如果保留 BIOS 默认映射，IRQ0 会落到向量 8，和 Double Fault 冲突。

因此当前系统必须把外部硬件中断搬到 0x20 之后，IRQ handler 才能与 CPU 异常清晰分开。相关逻辑在 [irq.c](file:///data25/lidg/lite/arch/x86/kernel/irq.c)。

### Q2.5: TSS 在当前项目里还重要吗？
**回答**：
重要，但用途已经很收敛了。当前项目不是走 x86 “硬件任务切换”，而是只使用 TSS 中少量字段，最关键的是：
- **`esp0/ss0`**：当 CPU 从用户态进入内核态时，切换到该任务的内核栈顶

因此 TSS 在当前实现里的作用可以概括为：
- “保证用户态陷入内核时，CPU 知道该切到哪根内核栈”

相关逻辑在 [gdt.c](file:///data25/lidg/lite/arch/x86/kernel/gdt.c) 和 [sched.c](file:///data25/lidg/lite/kernel/sched.c)。

---

## 3. 内存管理

### Q3.1: 当前系统的地址空间模型是什么？
**回答**：
当前是典型的 32 位高半区模型：
- 用户态可访问 `< TASK_SIZE` 的前 3GB
- 内核虚拟空间从 `PAGE_OFFSET` 开始，占高 1GB

在用户态地址空间中，每个任务有独立的页目录；在内核高半区，内核映射作为模板复制给各个进程。相关实现见 [memory.c](file:///data25/lidg/lite/mm/memory.c#L253-L283)、[mmap.c](file:///data25/lidg/lite/mm/mmap.c#L60-L113)。

### Q3.2: CR3 是什么？它放的是物理地址还是虚拟地址？
**回答**：
- CR3 的本名是 **Control Register 3**
- 在 32 位非 PAE 语境下，它常被叫做 **PDBR（Page Directory Base Register）**
- 当前项目和标准 x86 一样，CR3 装的是**页表根的物理地址语义**

对当前实现来说：
- `switch_pgd()` 会把当前 `pgd` 转换成物理地址，然后写入 CR3（见 [memory.c](file:///data25/lidg/lite/mm/memory.c#L269-L276)）
- 这也是为什么每个进程切换地址空间时，必须伴随页表切换

### Q3.3: 为什么打开分页前要先做恒等映射？
**回答**：
因为打开分页的瞬间，CPU 对地址的解释会立刻从“物理地址”变成“虚拟地址”。如果当前正在执行的地址没有被页表覆盖，就会立刻 page fault。

当前入口汇编先建立：
- 低端 4MB 恒等映射
- 高半区按需映射（覆盖内核末尾与 multiboot 启动数据）

然后打开分页并跳转到高地址继续执行（见 [boot.s](file:///data25/lidg/lite/arch/x86/boot/boot.s#L57-L122)）。

### Q3.4: 当前物理页分配器做到了什么程度？
**回答**：
当前已经不是最早期 bitmap 线性分配，而是一个**最小可用版 zoned buddy**：
- 有 `ZONE_DMA` / `ZONE_NORMAL`
- 有 `MAX_ORDER` 和 `free_area[]`
- 有 MIN/LOW/HIGH watermark
- 分配不足时会唤醒 `kswapd` 并尝试回收

实现入口见 [mmzone.c](file:///data25/lidg/lite/mm/mmzone.c#L13-L94)、[page_alloc.c](file:///data25/lidg/lite/mm/page_alloc.c#L141-L197)。

但它离 Linux 2.6 还有明显差距：
- 只有 lowmem，没有 highmem/NUMA
- 没有 per-cpu page list
- 没有 cpuset、migratetype、复杂 reserve 策略

### Q3.5: `mmap` / `brk` / Page Fault 在当前项目里是怎样配合的？
**回答**：
当前实现里：
- `mmap` / `brk` 主要维护 **VMA 元数据**
- 并不一定立刻创建 PTE
- 访问未映射页时，`do_page_fault()` 才根据 VMA 权限判断是否分配物理页并建立映射

相关逻辑：
- VMA 管理：[mmap.c](file:///data25/lidg/lite/mm/mmap.c#L115-L228)、[mmap.c](file:///data25/lidg/lite/mm/mmap.c#L456-L528)
- 缺页处理：[memory.c](file:///data25/lidg/lite/mm/memory.c#L479-L620)

### Q3.6: 当前 ELF 加载是 demand paging 吗？
**回答**：
不是。当前最大的特点是：
- **匿名堆/mmap** 更接近“按需分配”
- **ELF 的 `PT_LOAD` 段** 仍是 `exec` 时预分配并 `memcpy` 填充

也就是说，当前 loader 会先：
- 为每个 loadable segment 建立 VMA
- 逐页 `alloc_page + map_page_ex`
- 切换到新页表后 `memset + memcpy`

见 [exec.c](file:///data25/lidg/lite/fs/exec.c#L193-L263)。

### Q3.7: 当前 COW 和 rmap 做到什么程度？
**回答**：
当前已经有一条可运行的最小闭环：
- 写时 fault 时，`resolve_cow()` 检查 `PTE_COW`
- 若页被多方共享，则分配新页、复制内容、更新映射并修正 rmap
- `rmap_add/remove/dup` 用于记录“某个物理页当前被哪个 mm/vaddr 映射”

对应实现：
- COW fault：[memory.c](file:///data25/lidg/lite/mm/memory.c#L347-L389)
- rmap：[rmap.c](file:///data25/lidg/lite/mm/rmap.c#L1-L104)

但它仍明显简化于 Linux 2.6：
- 没有 anon/file rmap 分层
- 没有 `page_referenced()` / `try_to_unmap()`
- 还没有深度进入回收决策闭环

### Q3.8: 当前 swap / vmscan 是完整实现吗？
**回答**：
不是，是“可演示、可验证”的最小闭环：
- `vmscan` 会优先回收一页干净 page cache，再尝试线性扫描可换出页（见 [vmscan.c](file:///data25/lidg/lite/mm/vmscan.c#L50-L80)）
- `swap` 只有固定 `64` 个 slot，数据先被拷到 `kmalloc(PAGE_SIZE)` 缓冲中（见 [swap.c](file:///data25/lidg/lite/mm/swap.c#L12-L112)）

这和 Linux 2.6 的 active/inactive LRU、swap cache、后台回收线程还有较大差距。

### Q3.9: 当前实现和 Linux 2.6 的 MM 最大差距是什么？
**回答**：
最值得记住的几条：
- file-backed demand paging 还没打通
- VMA 仍是线性链表，不是 rbtree + mmap_cache
- rmap / reclaim / swap cache 仍是最小版
- dirty writeback / throttling / pdflush 风格机制还没有完整实现
- highmem/PAE/NUMA 尚未进入当前主线

也就是说，当前 MM 已经有“能跑”的主干，但还没有进入 Linux 2.6 那种“按页缓存、回收、换页、写回深度耦合”的状态。

---

## 4. 用户程序、ELF 与 PID 1

### Q4.1: 当前 loader 依赖 ELF 的哪些字段？
**回答**：
当前 loader 主要依赖：
- ELF Header：检查魔数、位宽、架构、入口地址、Program Header 位置
- Program Header：只关心 `PT_LOAD`
- `p_vaddr / p_offset / p_filesz / p_memsz / p_flags` 决定段应映射到哪里、需要拷多少、哪些字节清零、权限如何设置

见 [exec.c](file:///data25/lidg/lite/fs/exec.c#L110-L189)、[exec.c](file:///data25/lidg/lite/fs/exec.c#L249-L262)。

### Q4.2: 当前 PID 1 是怎么选出来的？
**回答**：
当前 `kernel_init()` 调用 `prepare_namespace()` 后，会优先执行参数指定的 init；若失败，则依次尝试：
- `/sbin/init`
- `/etc/init`
- `/bin/init`
- `/sbin/sh`
- `/bin/sh`

见 [main.c](file:///data25/lidg/lite/init/main.c#L127-L166)。

### Q4.3: 当前系统里的“用户态 shell”是怎么启动的？
**回答**：
它不是内置在内核里的常驻逻辑，而是：
- 被打包进 initramfs
- 根文件系统解包后变成普通可执行文件
- 最终通过 `task_exec_user()` 进入用户态执行

因此当前用户态 shell 仍是“普通 ELF 用户程序”，而不是一个内核内建 REPL。

---

## 5. 控制台、TTY 与输入输出

### Q5.1: 当前系统是不是只用了串口，没有用 VGA？
**回答**：
是的。**当前仅保留串口作为输出通道**，VGA 相关代码已暂时移除，后续会按 Linux 2.6 风格重做 VGA/vt 支持。

- `start_kernel()` 只初始化串口 `init_serial()`（见 [main.c](file:///data25/lidg/lite/init/main.c#L115-L125)）
- `init_serial()` 把 console/tty 输出目标加入 SERIAL（见 [serial.c](file:///data25/lidg/lite/drivers/tty/serial/serial.c#L30-L41)）

VGA 参考（保留用于后续重做）：
- VGA 文本模式本质是对物理地址 `0xB8000` 的显存窗口写入字符与属性字节（80x25，每字符 2 字节）
- 之前的实现是“串口 + VGA 同时输出”，并且会直接写 `0xB8000` 完成屏幕显示
- 详细解释与历史实现背景见归档文档：
  - [Kernel-QA.md:L513-L599](file:///data25/lidg/lite/Documentation/archived/Kernel-QA.md#L513-L599)

### Q5.2: console、tty、serial、VGA 在当前实现里的边界是什么？
**回答**：
- **serial**：具体硬件输出端
- **console**：内核日志/字符输出抽象，当前只分发到 serial
- **tty**：面向交互终端的抽象，既管理输入缓冲、前台进程，也把输出写到 serial
- **VGA（参考/暂时移除）**：历史上作为文本模式输出端，直接写 `0xB8000`；后续会按 Linux 2.6 的 vt/console 体系重做（参考 [Kernel-QA.md:L513-L599](file:///data25/lidg/lite/Documentation/archived/Kernel-QA.md#L513-L599)）

关键路径：
- console 输出分发：[console.c](file:///data25/lidg/lite/drivers/video/console/console.c#L17-L28)
- tty 输出分发：[tty.c](file:///data25/lidg/lite/drivers/tty/tty.c#L79-L86)
- 串口硬件：[serial.c](file:///data25/lidg/lite/drivers/tty/serial/serial.c#L7-L24)

### Q5.3: 键盘输入是怎么进到用户态程序的？
**回答**：
主线是：
- 键盘 IRQ -> 键盘驱动读扫描码
- 转成字符后调用 `tty_receive_char()`
- TTY 写入输入缓冲，必要时唤醒等待者
- 用户程序从 `/dev/tty` 或标准输入读取

TTY 输入逻辑在 [tty.c](file:///data25/lidg/lite/drivers/tty/tty.c#L97-L186)，设备节点在 [devtmpfs.c](file:///data25/lidg/lite/fs/devtmpfs/devtmpfs.c)。

### Q5.4: `/dev/console` 和 `/dev/tty` 在当前系统里是什么关系？
**回答**：
当前两者都存在于 devtmpfs，但语义不同：
- `/dev/console` 更偏“系统控制台”
- `/dev/tty` 更偏“当前终端接口”

在当前最小实现里，两者最终都落到 TTY/console 抽象上，因此路径还比较接近；但保留两个节点，是为了语义边界更接近 Linux。

---

## 6. 文件系统、rootfs 与挂载

### Q6.1: 当前根文件系统到底是什么？
**回答**：
当前根文件系统是 **ramfs**。

启动时：
- `vfs_init()` 注册并挂载 `ramfs` 为 `/`
- `populate_rootfs()` 再把 initramfs 中的 CPIO 内容解包进去

见 [namespace.c](file:///data25/lidg/lite/fs/namespace.c#L177-L187)、[initramfs.c](file:///data25/lidg/lite/init/initramfs.c#L44-L105)。

### Q6.2: `/proc`、`/dev`、`/sys`、`/mnt` 是怎么挂上的？
**回答**：
当前 `prepare_namespace()` 的顺序是：
- 先 `vfs_init()` 建根
- 解包 initramfs
- `mount /proc`
- `mount /dev` 为 devtmpfs
- `ksysfs_init()` 后再挂 `/sys`
- 把 `/dev/ram1` 上的 Minix 文件系统挂到 `/mnt`

见 [main.c](file:///data25/lidg/lite/init/main.c#L146-L166)。

### Q6.3: 当前 `vfsmount` 是怎么理解的？
**回答**：
当前实现里 `vfsmount` 很直接：
- `path`：挂载点路径字符串
- `sb`：对应 super_block
- `root`：该文件系统自己的根 dentry

`vfs_mount()` 把新文件系统根接入名字空间，并把 mount 记录链到全局挂载表中（见 [namespace.c](file:///data25/lidg/lite/fs/namespace.c#L114-L175)）。

### Q6.4: initramfs、rootfs、ramdisk 在当前实现里分别指什么？
**回答**：
- **initramfs**：bootloader 传进内存的一段 CPIO 字节流
- **rootfs**：当前命名空间里的根挂载点 `/`
- **ramfs**：当前承载 rootfs 的具体文件系统实现
- **ramdisk**：块设备抽象，例如 `/dev/ram0`、`/dev/ram1`

当前系统里：
- `/` 不是块设备挂出来的，而是 ramfs
- `/mnt` 才是块设备 `ram1` 上的 MinixFS

---

## 7. 设备模型、总线与块设备

### Q7.1: `driver_init()` 在当前系统里做了什么？
**回答**：
它建立当前最小设备模型的全局骨架：
- `platform_bus`
- `console_class`
- `tty_class`
- `devices/drivers/classes` 三个 kset
- 平台根设备 `platform-root`

见 [init.c](file:///data25/lidg/lite/drivers/base/init.c#L1-L55)、[core.c](file:///data25/lidg/lite/drivers/base/core.c#L1-L219)。

### Q7.2: 当前 `kobject / device / driver / bus / class` 的关系是什么？
**回答**：
可以把它们理解成一条逐层具体化的链：
- `kobject`：最底层可命名、可挂入层级结构的对象
- `device`：具体设备实例
- `driver`：能和设备匹配并 probe 的驱动
- `bus`：设备与驱动的组织与匹配域
- `class`：面向用户语义的分组（如 tty、console）

当前实现已经支持：
- 设备注册
- 驱动注册
- 简单匹配绑定
- `/sys/devices`、`/sys/bus`、`/sys/class` 的可见化

### Q7.3: PCI 和 NVMe 在当前系统里走的是什么路径？
**回答**：
1.  PCI 总线通过 initcall 初始化并扫描 bus 0，枚举设备（见 [pci.c](file:///data25/lidg/lite/drivers/pci/pci.c#L387-L424)）。
2.  NVMe 驱动注册到 PCI bus，按 class code 匹配 `0x01/0x08`（见 [nvme.c](file:///data25/lidg/lite/drivers/nvme/nvme.c#L48-L56)、[nvme.c](file:///data25/lidg/lite/drivers/nvme/nvme.c#L175-L184)）。
3.  当前 NVMe 仍是 testing mode：
   - 不做完整控制器命令通路
   - 直接创建一个 16MB block device namespace
   - 通过设备模型注册出来

因此现在的 NVMe 更接近“把 PCI 枚举与块设备注册链路打通”，还不是完整 NVMe 协议实现。

### Q7.4: devtmpfs 在当前系统里起什么作用？
**回答**：
devtmpfs 负责把抽象设备节点暴露到 `/dev`，当前至少包含：
- `/dev/console`
- `/dev/tty`
- 动态注册的块设备节点

这样用户态不需要自己手动创建设备节点，就能访问 tty/console/ramdisk/NVMe 等设备。可参考 [devtmpfs.c](file:///data25/lidg/lite/fs/devtmpfs/devtmpfs.c)。

---

## 8. 块层、buffer cache 与 page cache

### Q8.1: 当前块设备 I/O 的最小主链路是什么？
**回答**：
当前已经有一条简化但完整的链路：
- `block_device_read/write()`
- 构造 `bio`
- `submit_bio()`
- request queue
- `request_fn`
- 落到内存块设备后端

在当前 ramdisk/内存块设备模型里，最终数据直接拷到 `bdev->data`（见 [blkdev.c](file:///data25/lidg/lite/drivers/block/blkdev.c#L21-L58)、[blkdev.c](file:///data25/lidg/lite/drivers/block/blkdev.c#L92-L148)）。

### Q8.2: buffer cache 和 page cache 在当前项目里是怎么分工的？
**回答**：
- **buffer cache**：面向块设备块号，主要给文件系统元数据或块级访问使用
- **page cache**：面向 inode/address_space 的文件页缓存，主要给文件数据读写和 writeback 使用

当前实现里：
- buffer cache 由 `bread / mark_buffer_dirty / sync_dirty_buffer` 提供（见 [buffer.c](file:///data25/lidg/lite/fs/buffer.c#L126-L204)）
- page cache 由 `address_space + page_cache_entry` 提供（见 [pagemap.h](file:///data25/lidg/lite/include/linux/pagemap.h#L1-L45)、[filemap.c](file:///data25/lidg/lite/mm/filemap.c#L90-L166)）

### Q8.3: 当前 writeback 和回收做到了什么程度？
**回答**：
当前已经有：
- 脏页计数
- `writeback_flush_all()` 的同步回写
- `page_cache_reclaim_one()` 的单页 clean cache 回收
- `vmscan + swap` 的最小联动

但还没有：
- Linux 2.6 风格的 `balance_dirty_pages()`
- pdflush/kupdate 风格后台线程
- 真实 LRU / aging / congestion feedback

见 [filemap.c](file:///data25/lidg/lite/mm/filemap.c#L261-L311)、[vmscan.c](file:///data25/lidg/lite/mm/vmscan.c#L1-L80)、[swap.c](file:///data25/lidg/lite/mm/swap.c#L12-L112)。

---

## 9. 现状总结

### Q9.1: 如果只用一句话概括当前 Lite OS 的实现状态，应该怎么说？
**回答**：
当前 Lite OS 已经打通了一条“**32 位 x86、单核、最小用户态、基础 VFS、最小设备模型、匿名页 + COW + 最小 swap/reclaim、ramdisk/Minix/NVMe 测试链路**”的可运行主线，但离 Linux 2.6 仍有明显差距，主要体现在：
- file-backed demand paging
- 完整 VMA/rmap/reclaim 体系
- 更成熟的页缓存/写回/块层协同
- 更完整的设备模型与驱动协议实现

因此最适合把它看成：
- **已经有真实主链路的教学型内核**
- 而不是“只有 demo，没有操作系统主干”的原型
