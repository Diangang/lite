# Lite OS QA（按当前实现梳理）


## 文档定位
- 这是 **当前实现行为** 的权威问答文档。
- 当其它规划、路线、问题日志与本文件冲突时，以本文件和实际源码为准。

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

### Q2.5: 中断和 `i8259` 是什么关系？为什么以前看起来没有 `i8259.c` 也能正常处理中断？
**回答**：
要先区分“哪一类中断”：
- **CPU 异常**：不经过 `i8259`
- **`int 0x80` 系统调用**：不经过 `i8259`
- **传统外设硬件中断**：经过 `i8259`

当前 Lite 里：
- 异常入口走 [interrupt.s](file:///data25/lidg/lite/arch/x86/kernel/interrupt.s#L9-L34) 的 `isr_common_stub`
- 硬件 IRQ 入口走 [interrupt.s](file:///data25/lidg/lite/arch/x86/kernel/interrupt.s#L36-L87) 的 `irq_common_stub`
- 真实生效的外部中断控制器是 [i8259.c](file:///data25/lidg/lite/arch/x86/kernel/i8259.c#L53-L75)

可以把当前外设中断链路理解为：
- **设备 -> i8259 -> CPU vector 32..47 -> `irq_common_stub` -> `irq_handler()` -> `irq_desc/irq_chip/设备 handler`**

所以“以前没有独立 `i8259.c` 也能跑”的原因，不是系统不需要 PIC，而是：
- 早期实现里，PIC remap / EOI / mask/unmask 这类逻辑更可能散落在 generic IRQ 初始化或处理中
- 现在只是把这些 legacy PIC 语义显式收敛到了 [i8259.c](file:///data25/lidg/lite/arch/x86/kernel/i8259.c)

这一步的意义主要是模型整理，而不是凭空增加了新功能：
- generic IRQ 层只保留 `irq_desc` / `irq_chip` 这种 Linux-shaped 抽象
- legacy PIC 的具体控制器行为从 generic 路径里剥离出来，放到专门的 arch 控制器层

### Q2.6: `APIC` 和 `IO_APIC` 分别是什么？和 `i8259` 怎么区分？
**回答**：
可以把它们看成“PIC 之后的新一代中断体系里的两个不同角色”：

- **`i8259`**：
  - 老式 PIC
  - 负责传统外设 IRQ 线
  - 当前 Lite 真正运行的是这套模型
- **local APIC（当前代码里简写为 `APIC`）**：
  - 每个 CPU 本地一个
  - 更偏向 CPU 本地中断：本地 timer、IPI、接收路由到本 CPU 的中断向量
- **`IO_APIC`**：
  - 更偏向外设入口侧
  - 负责把外设 IRQ 路由到某个 CPU 的某个 vector

一个直观理解方式是：
- `IO_APIC` 像“外设中断分发器”
- local APIC 像“每个 CPU 的本地收件站”
- `i8259` 像更老、更简单的总机

在 Linux 风格的 APIC 模型中，典型外设中断链路会更像：
- **设备 -> IO_APIC -> 某个 CPU 的 local APIC -> 对应 vector handler**

而本地 timer / IPI 则通常不经过 `IO_APIC`，而是直接属于 local APIC 的职责。

### Q2.7: 当前 Lite 里的 `APIC` / `IO_APIC` 到底工作到什么程度？
**回答**：
当前代码已经把边界建出来了，但运行时还没有启用真实 APIC 模式：

- [apic.c](file:///data25/lidg/lite/arch/x86/kernel/apic.c)：
  - `pic_mode = 1`
  - `lapic_enabled = 0`
  - 说明系统仍然停留在 PIC 运行模式
  - 但已经预留并分层了 Linux-shaped 向量：
    - `LOCAL_TIMER_VECTOR`
    - `RESCHEDULE_VECTOR`
    - `CALL_FUNCTION_VECTOR`
    - `ERROR_APIC_VECTOR`
    - `SPURIOUS_APIC_VECTOR`
- [io_apic.c](file:///data25/lidg/lite/arch/x86/kernel/io_apic.c)：
  - 目前只是 no-op placeholder
  - 还没有真正的 IOAPIC redirection table / 路由逻辑
- [irq_install](file:///data25/lidg/lite/arch/x86/kernel/isr.c#L219-L245)：
  - 会先调用 `apic_init()` 和 `io_apic_init()`
  - 但如果不是 `pic_mode`，当前实现会直接 panic

因此现在的真实状态是：
- **PIC / i8259：真的在跑**
- **APIC / IO_APIC：边界、向量和 handler 所有权已经建好，但仍是 placeholder**

换句话说，Lite 当前不是 APIC mode，只是已经开始把代码组织整理成更接近 Linux 2.6 的 APIC / IOAPIC 结构。

### Q2.8: TSS 在当前项目里还重要吗？
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
- `init_serial()` 把 console/tty 输出目标加入 SERIAL（见 [8250.c](file:///data25/lidg/lite/drivers/tty/serial/8250.c#L73-L94)）

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
- console 输出分发：[printk.c](file:///data25/lidg/lite/kernel/printk.c#L8-L45)
- tty 输出分发：[tty.c](file:///data25/lidg/lite/drivers/tty/tty.c#L79-L86)
- 串口硬件（8250/16550A）：[8250.c](file:///data25/lidg/lite/drivers/tty/serial/8250.c#L1-L33)

### Q5.3: 键盘输入是怎么进到用户态程序的？
**回答**：
主线是：
- 键盘 IRQ -> 键盘驱动读扫描码
- 转成字符后调用 `tty_receive_char()`
- TTY 写入输入缓冲，必要时唤醒等待者
- 用户程序从 `/dev/tty` 或标准输入读取

TTY 输入逻辑在 [tty.c](file:///data25/lidg/lite/drivers/tty/tty.c#L97-L186)，设备节点与 `/dev` 挂载逻辑在 [devtmpfs.c](file:///data25/lidg/lite/drivers/base/devtmpfs.c)。

### Q5.3B: `serio` / `i8042` / `atkbd` 三者在当前系统里的关系是什么？和 Linux 一样吗？
**回答**：
当前 Lite 里这三者的分工与 Linux2.6 的 “i8042 -> serio core/bus -> atkbd driver” 模型一致，但实现是最小子集。

为什么不把键盘逻辑全写进 `i8042`：
- Linux 语义里 `i8042` 是控制器/传输层（serio port provider），不是“键盘协议驱动”。它负责从硬件读出字节并发布端口；端口上可能挂键盘、也可能挂 PS/2 鼠标（aux port）等设备。
- `atkbd` 的角色是协议层：消费 serio 字节流并把 AT 键盘扫描码解释为更高层的输入语义。把协议层放在 `atkbd` 而不是 `i8042`，可以避免把控制器代码变成“键盘+鼠标+…的大杂烩”，也更贴近 Linux 的分层（未来要支持鼠标时应新增另一个 serio driver，而不是继续堆在 `i8042`）。

“真硬件”和“抽象层”怎么理解：
- 真正的硬件链路通常是：PS/2 键盘（设备本体） -> i8042 控制器接口/寄存器（主机侧控制器逻辑） -> IRQ1 -> CPU。
- `i8042` 驱动做的是“硬件接口层”：响应 IRQ，从 i8042 数据端口读出一个字节（扫描码/协议字节），然后交给 serio core（Linux 对应路径里也是 `serio_interrupt()`）。
- `serio bus` / `serio port` / `atkbd` 都是软件抽象（虚拟概念），但它们承载的是 Linux driver model 的分层：把“控制器/传输层（产出字节流）”与“协议层（解释字节流）”解耦，使一个控制器可以有多个 port（kbd/aux），一个 port 上可以绑定不同的协议驱动（例如键盘/鼠标）。

职责分层（对齐 Linux 的概念边界）：
1. `i8042`：控制器/port provider
   - 负责初始化 8042 硬件与 IRQ1，并把读取到的扫描码字节送进 serio core：`serio_interrupt(&i8042_port, scancode)`（见 [i8042.c](file:///data25/lidg/lite/drivers/input/serio/i8042.c#L15-L22)）。
   - 负责填好端口静态属性：`i8042_port.id`、`i8042_port.parent`、必要时 `i8042_port.dev.release`，然后只调用 `serio_register_port()` 发布端口（见 [i8042.c](file:///data25/lidg/lite/drivers/input/serio/i8042.c#L54-L67)）。
2. `serio`：serio core + serio_bus（driver core 接入点）
   - 提供 `serio_bus.match/probe/remove`，通过 `id_table` 完成匹配绑定；probe 成功后缓存 `serio->drv`，IRQ 快路径直接分发到 `drv->interrupt()`（见 [serio.c](file:///data25/lidg/lite/drivers/input/serio/serio.c#L38-L155)）。
   - 对外只暴露 “发布端口”入口：`serio_register_port()`。`serio_init_port()` 是内部 helper，不再作为公共 API（避免外部代码把 init/register 拆开到处调用）（见 [serio.c](file:///data25/lidg/lite/drivers/input/serio/serio.c#L90-L120)、[serio.h](file:///data25/lidg/lite/include/linux/serio.h#L29-L59)）。
3. `atkbd`：serio driver consumer
   - 注册 `struct serio_driver atkbd_drv`，带 `id_table` 仅匹配 `SERIO_8042` 端口；匹配后由 driver core 调用 connect，再由 `serio_interrupt()` 分发字节到 `atkbd_interrupt()`（见 [atkbd.c](file:///data25/lidg/lite/drivers/input/keyboard/atkbd.c#L38-L63)）。

与 Linux 的一致点：
- 模型关系一致：controller/provider 发布 port，serio 作为 bus/core 做匹配绑定，atkbd 作为 serio driver 绑定并消费数据。
- 匹配语义一致：`serio_device_id` + `SERIO_ANY` 通配 + `{0}` 终止表。

仍然简化/不一致的点（Lite 是“少实现”，不是发明新模型）：
- Linux 的 `atkbd` 会上报到 Linux input subsystem（`input_dev` 事件）；Lite 目前是把扫描码转字符后直接喂给 `tty_receive_char()`，因此没有完整 input event 层。
- Linux 的 i8042 通常会注册多个 port（kbd/aux）并支持更多 port ops；Lite 目前只实现满足串口控制台交互的最小闭环。

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
- 平台根设备 `platform`（内部锚点，用于挂载 platform 设备树）

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

### Q7.3: 为什么当前有 `platform_device_register_simple()`，却没有非 `simple` 的 `platform_device_register()`？
**回答**：
当前 Lite 里只有 [`platform_device_register_simple()`](file:///data25/lidg/lite/drivers/base/platform.c#L118-L158)，没有实现非 `simple` 的 [`platform_device_register()`](file:///data25/lidg/lite/Documentation/archived/Kernel-QA.md#L1597-L1597)。

它之所以叫 `simple`，是因为它只覆盖“最小平台设备注册”这条路径：
- 分配 `struct platform_device`
- 填 `name` 和 `id`
- 拼实例名，例如 `serial8250`
- 调 `device_initialize()` / `device_add()`
- 把设备挂到 `platform_bus`

这和当前 Lite 的平台设备模型是匹配的，因为 [`struct platform_device`](file:///data25/lidg/lite/include/linux/platform_device.h#L16-L20) 目前只有：
- `name`
- `id`
- `dev`

也就是说，当前还没有 Linux 里更完整的那套资源、platform data、DMA/firmware 节点等需求，因此“只给 `name + id` 就注册”的 `simple` helper 已经足够覆盖现有场景。

如果对照 Linux 语义，可以这样理解：
- `platform_device_register_simple()`：帮调用者分配对象并填最小字段，适合“快速注册一个简单平台设备”
- `platform_device_register()`：通常要求调用者先准备好完整 `struct platform_device`，再做注册，灵活性更高

所以当前仓库里的真实状态是：
- **有** `platform_device_register_simple()`
- **没有** 非 `simple` 的 `platform_device_register()`
- 这反映的是 **平台设备基础设施仍是最小子集**，而不是遗漏了一个已经被广泛依赖的接口

### Q7.4: PCI 和 NVMe 在当前系统里走的是什么路径？
**回答**：
1.  PCI 总线通过 initcall 初始化并扫描 bus 0，枚举 `pci_dev` 并注册进 device model（见 [pci.c](file:///data25/lidg/lite/drivers/pci/pci.c#L709-L747)、[pci_register_function](file:///data25/lidg/lite/drivers/pci/pci.c#L423-L597)）。
2.  PCI driver core 通过 `id_table` + `pci_bus_match()` 完成匹配，然后进入“桥接 probe”（`device_driver.probe` -> `pci_driver.probe`）（见 [pci_bus_match](file:///data25/lidg/lite/drivers/pci/pci.c#L640-L657)、[pci_driver_probe](file:///data25/lidg/lite/drivers/pci/pci.c#L659-L676)）。
3.  NVMe 驱动注册到 PCI bus，按 class code `0x01/0x08` 匹配（见 [nvme_pci_ids](file:///data25/lidg/lite/drivers/nvme/nvme.c#L645-L649)、[nvme_pci_probe](file:///data25/lidg/lite/drivers/nvme/nvme.c#L651-L705)）。
4.  当前 NVMe 仍是“打通链路”的简化实现：重点在把 PCI 枚举、MMIO、队列、gendisk 注册、devtmpfs/sysfs 暴露串成闭环，还不是完整 NVMe 协议栈。

### Q7.5: devtmpfs 在当前系统里起什么作用？
**回答**：
devtmpfs 负责把抽象设备节点暴露到 `/dev`，当前至少包含：
- `/dev/console`
- `/dev/tty`
- 动态注册的块设备节点

这样用户态不需要自己手动创建设备节点，就能访问 tty/console/ramdisk/NVMe 等设备。当前 devtmpfs 已收敛到 [devtmpfs.c](file:///data25/lidg/lite/drivers/base/devtmpfs.c)，底层承载使用 ramfs。

### Q7.6: 为什么会有 “driver core 的 probe” 和 “PCI/NVMe 自己的 probe” 两层？
**回答**：
Linux 也是两层分工，Lite 当前做法与其一致。
1.  driver core 的 `device_driver.probe(struct device *)` 是通用入口：负责绑定关系（`dev->driver`）、sysfs link、uevent（bind/unbind）等共性动作（见 [driver_probe_device](file:///data25/lidg/lite/drivers/base/driver.c#L173-L196)）。
2.  PCI 子系统的 `pci_driver.probe(struct pci_dev *, const struct pci_device_id *)` 是总线专用入口：负责把抽象 `device` 转成 `pci_dev`，并把命中的 `id_table` 条目传给具体驱动（见 [pci_driver_probe](file:///data25/lidg/lite/drivers/pci/pci.c#L659-L676)）。
3.  具体驱动（例如 NVMe）只实现它关心的硬件语义（队列、MMIO、命令等），不需要重复实现通用绑定流程（见 [nvme_pci_probe](file:///data25/lidg/lite/drivers/nvme/nvme.c#L651-L705)）。

### Q7.6: “设备注册后自动匹配驱动、驱动注册后自动匹配设备”，Linux 也是这样吗？
**回答**：
是的。这是 Linux driver core 的基本语义，顺序不重要，最终都会收敛到同样的绑定结果。
1.  device 侧：`device_add()` 后，如果该 bus 开启 `drivers_autoprobe`，会触发 `device_attach()` 遍历 bus 的 driver 集合尝试 probe（见 [device_add](file:///data25/lidg/lite/drivers/base/core.c#L345-L366)、[device_attach](file:///data25/lidg/lite/drivers/base/core.c#L307-L329)）。
2.  driver 侧：`driver_register()` 后，如果该 bus 开启 `drivers_autoprobe`，会触发 `driver_attach()` 遍历该 bus 已有设备尝试 probe（见 [driver_register](file:///data25/lidg/lite/drivers/base/driver.c#L219-L239)、[driver_attach](file:///data25/lidg/lite/drivers/base/driver.c#L204-L217)）。

### Q7.7: uevent 机制是什么？Linux 必须要它吗？Lite 当前怎么做的？
**回答**：
1.  Linux uevent：设备 `add/remove/bind/unbind/change` 等事件发生时，内核会把一组 `KEY=VALUE` 环境变量通过 netlink（`NETLINK_KOBJECT_UEVENT`）广播给用户态（典型消费者是 udev/systemd-udevd），用于创建设备节点、自动加载模块、执行规则等。
2.  “必须吗”：对内核能运行不是硬必须；但对热插拔、自动创 `/dev`、自动加载驱动等“像 Linux 一样可用”的用户态体系来说基本是必须。
3.  Lite 当前实现：以简化版方式把事件写入内核缓冲区，并通过 `/sys/kernel/uevent` 以文本形式导出给用户态读取（见 [uevent.c](file:///data25/lidg/lite/drivers/base/uevent.c#L166-L257)、[ksysfs.c](file:///data25/lidg/lite/kernel/ksysfs.c#L65-L106)）。
4.  语义边界（对齐 Linux）：Lite 当前只发 driver core 的 `ACTION=add/remove/bind/unbind`，其他“内部状态”（例如 BAR 分配、PCIe capability 识别）用日志表达，不再用自造 `ACTION=` 字符串。

### Q7.8: “监听 netlink uevent” 最底层是什么意思？会有“内核中断用户态”吗？
**回答**：
1.  监听的本质是用户态在 socket 上阻塞等待：`recvmsg()` 或 `poll/epoll_wait()` 把线程挂到等待队列里睡眠。
2.  内核产生 uevent 时，把消息投递到 netlink socket 的接收队列，并 `wake_up()` 等待队列，使阻塞系统调用返回“可读”。
3.  没有“内核直接中断用户态并执行回调函数”这种机制。硬件中断只会进入内核态处理；用户态被唤醒依赖调度器把该线程变为 runnable。

### Q7.9: attribute / attribute_group 有什么用？
**回答**：
1.  `attribute` 表示一个 sysfs 文件节点（名字/权限），具体读写由 `sysfs_ops->show/store` 分发。
2.  `attribute_group` 用于成组管理一批 attribute，并可通过 `is_visible()` 动态控制某些属性是否对某个对象实例可见。
3.  例子：`/sys/kernel/version/uptime/uevent_helper/uevent_seqnum` 是一组 kernel 属性，当前通过 `kernel_kobj + sysfs_create_group()` 注册；其中 `/sys/kernel` 下的几个文件使用 `kobj_attribute` 直接提供 `show/store`（见 [ksysfs.c](file:///data25/lidg/lite/kernel/ksysfs.c)、[sysfs.c](file:///data25/lidg/lite/fs/sysfs/sysfs.c)）。

### Q7.10: PCI 的 `id_table` 是什么？`pci_bus_match()` 在做什么？
**回答**：
1.  `id_table` 是 PCI 驱动声明“支持哪些设备/哪些 class”的匹配表（以 `{0}` 结尾），每一项是 `struct pci_device_id`（vendor/device/subvendor/subdevice/class/class_mask/driver_data）（见 [pci.h](file:///data25/lidg/lite/include/linux/pci.h#L36-L53)）。
2.  `pci_bus_match()` 遍历 `id_table`，对每条规则调用 `pci_match_one()`，命中就认为该驱动可以绑定该设备（见 [pci_bus_match](file:///data25/lidg/lite/drivers/pci/pci.c#L640-L657)、[pci_match_one](file:///data25/lidg/lite/drivers/pci/pci.c#L624-L638)）。

### Q7.11: PCI 和 PCIe 的关系（代码逻辑上）是什么？
**回答**：
1.  PCIe 不是独立的“另一套设备模型”，而是 PCI 的链路/能力扩展：枚举、BDF、配置空间等软件模型仍然走 PCI 这套。
2.  Lite 里 PCIe 当前做的是 capability 识别：在 capability list 里找 `PCI_CAP_ID_EXP`，找到就记录 `pdev->pcie_cap`，供上层驱动（如 NVMe）判断（见 [pcie_scan_device](file:///data25/lidg/lite/drivers/pci/pcie/pcie.c#L11-L23)）。
3.  PCI 枚举流程在创建 `pci_dev` 后调用 `pcie_scan_device()`，因此 PCIe 逻辑是“挂在 PCI 扫描链路上的能力识别”，不是另起一套扫描（见 [pci_register_function](file:///data25/lidg/lite/drivers/pci/pci.c#L423-L597)）。

### Q7.12: BDF 是什么？代码里怎么表示？为什么一个 bus 是 32 个 device、一个 device 是 8 个 function？
**回答**：
1.  BDF 是 `Bus:Device.Function` 的寻址三元组。Lite 里 `struct pci_dev` 用 `bus + devfn` 表示，其中 `devfn=(dev<<3)|func`（见 [pci.h](file:///data25/lidg/lite/include/linux/pci.h#L12-L34)、[pci_register_function](file:///data25/lidg/lite/drivers/pci/pci.c#L423-L456)）。
2.  32 和 8 是 PCI/PCIe 配置空间寻址位宽决定的硬上限：device number 5 bit -> 32，function number 3 bit -> 8；PCIe 仍沿用该上限。
3.  “multi-function”指同一 slot（同一 bus+device）下暴露多个 function（0..7），常见于同一颗芯片/同一张卡的多个逻辑功能块。

### Q7.13: `prog_if` 是什么的缩写？有什么用？
**回答**：
`prog_if` 是 Programming Interface（编程接口）的缩写，是 class code 三元组的第 3 个字节，用于在相同 class/subclass 下区分寄存器语义不同的接口实现（见 [pci_register_function](file:///data25/lidg/lite/drivers/pci/pci.c#L423-L456)）。

### Q7.14: 为什么 `lspci -t` 里会有很多 `pci0000:xx`（很多 bus）？这些都对应“物理硬件”吗？
**回答**：
1.  `pci0000:xx` 更多表示同一 domain 下的不同 bus 段。bus 的增多通常来自桥/root port/PCIe switch，下游会分配 secondary bus number，所以拓扑复杂时 bus 看起来很多。
2.  `lspci` 里大量 “Intel Corporation Device ...” 往往是 Root Complex 集成的内部功能块（RCiEP/uncore），它们对软件呈现为 PCI function，但不一定对应可拔插的外设卡。
3.  外设卡/盘（NIC/NVMe/HBA/GPU）通常是叶子上的 endpoint（例如 `0000:98:00.0`），并且经常在某个桥后面出现（`...-[98]----00.0` 这种形态）。

### Q7.15: “host bridge” 和 “root complex” 为什么会有两个叫法？
**回答**：
1.  host bridge 更偏传统 PCI/OS 视角：CPU/内存体系与 PCI 总线体系的桥接逻辑。
2.  root complex 是 PCIe 规范术语：PCIe 根节点系统，包含 root port、路由、错误处理等更大范围的根侧逻辑。
3.  工程语境中两者常混用，因为都在指“PCIe 根侧入口”，但 root complex 概念范围通常更大。

### Q7.16: `0x80000000 | bus<<16 | dev<<11 | func<<8 | (offset & 0xFC)` 是 PCI 协议规定的吗？
**回答**：
这不是 PCIe 传输层协议的通用格式，而是传统 PCI 配置访问机制（CF8/CFC，Configuration Mechanism #1）对 `CONFIG_ADDRESS` 寄存器的规定格式。在使用该机制的平台上它是固定的；而 PCIe 也可以通过 ECAM（MMIO）方式访问配置空间，不走 CF8 这套位拼装。

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

在当前 ramdisk/内存块设备模型里，最终数据直接拷到 `backend->data`（vmalloc 出来的内存），见 [ramdisk_request_fn](file:///data25/lidg/lite/drivers/block/ramdisk.c#L43-L75)。

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

### Q8.4: `gendisk`、`block_device`、`request_queue` 在 Linux 里是什么关系？Lite 现在对齐到哪一步了？
**回答**：
1.  Linux 2.6 语义：`request_queue` 是“每块盘/每个 gendisk 一份”，所有分区与打开实例共享队列（见 [genhd.h](file:///data25/lidg/lite/linux2.6/include/linux/genhd.h#L100-L110)）。
2.  Lite 现状（已对齐）：`struct gendisk` 持有 `queue`，I/O 提交路径从 `bio->bi_bdev->disk->queue` 取队列（见 [blkdev.h](file:///data25/lidg/lite/include/linux/blkdev.h)、[blk-core.c](file:///data25/lidg/lite/block/blk-core.c)）。
3.  仍然简化的点：Lite 当前还没有 Linux 那种“同一 gendisk 下多个 block_device（分区/多打开实例）”的完整语义；多数场景仍是一盘一 bdev 的最小闭环，但队列所有权已经先对齐到正确层级。

### Q8.5: ramdisk 的“后端”在哪里？为什么注册 `ram0/ram1` 不需要真实硬件？
**回答**：
1.  后端是 `vmalloc(size)` 出来的一段内存，作为块设备的存储空间（见 [ramdisk_bdev_init](file:///data25/lidg/lite/drivers/block/ramdisk.c#L90-L118)）。
2.  request_fn 里把 bio 的数据 memcpy 到这段内存或从这段内存 memcpy 出来，形成最小块设备闭环（见 [ramdisk_request_fn](file:///data25/lidg/lite/drivers/block/ramdisk.c#L43-L75)）。
3.  Linux 上也有同类概念（brd/ramdisk），本质同样是“用内存做后端”，只是 Linux 的块层、回写、队列与参数配置更完整。

### Q8.6: Linux 的 `backing_dev_info`（BDI）是干什么的？和块设备是什么关系？
**回答**：
1.  BDI 是 writeback/page cache 的“回写端点”抽象：用于描述脏页回写、拥塞、节流、设备能力等。它服务的是 page cache/writeback，而不是直接替代 request_queue。
2.  对块设备来说，BDI 往往绑定到“整盘/队列”这一层级，并通过 `bdi->dev` 关联到该盘的 `struct device`（`/sys/class/block/<disk>` 视角的设备对象），以便统计、限速和归属管理。
3.  Lite 当前尚未实现 Linux 的完整 writeback/BDI 体系，因此暂时没有对应结构体是正常的；未来若对齐 writeback，BDI 会更靠近“每盘一份”的模型。

### Q8.7: Linux 上 `sda`、`sda1`、`sda2` 分别对应几个 `device`/`block_device`/`request_queue`/`gendisk`？
**回答**：
1.  `struct device`：通常有 3 个，整盘 `sda` 1 个，分区 `sda1` 1 个，分区 `sda2` 1 个。
2.  `struct block_device`：通常也对应 3 个，整盘 1 个，分区各 1 个（语义是“打开实例/分区对象”）。
3.  `request_queue`：通常 1 个（整盘共享），所有分区 I/O 最终进同一队列。
4.  `gendisk`：通常 1 个（整盘），分区通过 `gendisk->part/hd_struct` 之类结构描述。

### Q8.8: Linux 2.6 的 NVMe（`linux2.6/drivers/nvme/host/pci.c`）里，每盘的 `gendisk/request_queue/block_device` 分别在哪个阶段创建？
**回答**：
先说明“每盘”的粒度：Linux NVMe 把一个 namespace 当作一个块盘（`nvme_ns`），因此这里的“每盘”指“每个 namespace”。
1.  `request_queue`（每 namespace 一份）：在创建 namespace 时由 NVMe 驱动直接创建，代码是 `ns->queue = blk_mq_init_queue(&dev->tagset);`（见 [pci.c](file:///data25/lidg/lite/linux2.6/drivers/nvme/host/pci.c#L2246-L2285)）。
2.  `gendisk`（每 namespace 一份）：同样在创建 namespace 时由 NVMe 驱动分配，代码是 `disk = alloc_disk_node(0, node);`，并设置 `disk->queue = ns->queue`、`disk->private_data = ns`（见 [pci.c](file:///data25/lidg/lite/linux2.6/drivers/nvme/host/pci.c#L2264-L2294)）。
3.  盘对外注册：`add_disk(ns->disk);` 会把该盘注册到系统（sysfs/devfs/hotplug 等），这是“盘变得可见”的关键节点（见 [pci.c](file:///data25/lidg/lite/linux2.6/drivers/nvme/host/pci.c#L2305-L2319)）。
4.  `block_device`（按 dev_t 获取/创建，不由 NVMe 驱动直接分配）：
    - NVMe 驱动在 `add_disk` 之后，会用 `bdget_disk(ns->disk, 0)` 取“整盘 bdev”，并 `blkdev_get()` 打开一次触发 `blkdev_reread_part()`（用于读分区表），这一步通常会促使整盘 `block_device` 出现并完成绑定（见 [pci.c](file:///data25/lidg/lite/linux2.6/drivers/nvme/host/pci.c#L2307-L2318)）。
    - 更底层的 bdev 绑定逻辑发生在块设备 open 路径：`do_open()` 会把 `bdev->bd_disk` 绑定到 `gendisk`，必要时触发 `rescan_partitions()` 并设置 BDI（见 [block_dev.c](file:///data25/lidg/lite/linux2.6/fs/block_dev.c#L560-L623)）。
5.  分区对象（不是 bdev 本身）：分区扫描会通过 `add_partition()` 分配 `hd_struct` 并注册其 kobject（见 [check.c](file:///data25/lidg/lite/linux2.6/fs/partitions/check.c#L289-L314)）。

### Q8.9: Lite 现在的 `scsi + virtio-scsi-pci` 是怎么挂到块层上的？和 Linux 2.6 的对应关系是什么？
**回答**：
1.  Linux 2.6 对应项：
    - `drivers/scsi/virtio_scsi.c`：`struct virtio_scsi` 作为 HBA
    - `include/scsi/scsi_host.h` / `scsi_device` / `sd`：SCSI host、device、disk 三层模型
2.  Lite 现状（按 Linux 术语的最小闭环）：
    - `drivers/virtio/virtio.c`：最小 `virtio` 总线（`virtio_bus_type`）与 `virtio_driver/virtio_device` 注册路径
    - `drivers/virtio/virtio_pci.c`：最小 `virtio-pci` transport（`pci_driver`），负责把 PCI 上的 virtio 设备实例化为 `virtio_device`，并提供 `find_vqs/del_vqs` 以创建/销毁 virtqueue
    - `drivers/virtio/virtqueue.c`：最小 virtqueue/vring helper（对应 Linux `drivers/virtio/virtio_ring.c` 的一小部分）
    - `drivers/scsi/virtio_scsi.c`：前端 `virtio_driver`（不是 `pci_driver`），在 virtio 总线上探测 `VIRTIO_ID_SCSI`，并通过 virtqueue 提交 SCSI 命令
    - `drivers/scsi/scsi.c` 提供 `Scsi_Host`、`scsi_target`、`scsi_device`、`scsi_disk` 的最小对象模型与 class 视图
    - Lite 现在新增了最小 `scsi_scan_target()` / `scsi_scan_host_selected()` 接口，把 host 边界校验与 target/LUN 遍历下沉到 SCSI 层，而不是留在 `virtio_scsi.c` 里做启发式停扫
    - `drivers/scsi/sd.c` 把 `scsi_disk` 注册为块盘，落成 `/dev/sda`
3.  启动路径：
    - PCI 命中 `virtio-scsi-pci`（transport 层）
    - `virtio-pci` 创建并注册 `virtio_device`（前端驱动匹配发生在 virtio 总线上）
    - 建 `Scsi_Host`
    - 把 virtio-scsi config 里的 `max_channel/max_target/max_lun` 灌到 `Scsi_Host`
    - `virtio_scsi.c` 现在和 Linux 一样在设置好 `shost->max_channel/max_id/max_lun` 后直接调用 `scsi_scan_host(shost)`，由 SCSI 层按 host 边界决定扫描范围
    - `scsi.c` 负责 host 边界校验、channel/target/LUN 遍历，以及 `TEST UNIT READY`、`INQUIRY`、`READ CAPACITY`
    - Lite 现在也补了最小 `scsi_target` 对象，`scsi_device` 不再直接挂在 `Scsi_Host` 下，而是挂在对应的 `targetH:C:T` 节点下；这和 Linux 把 `sdev` 组织在 `scsi_target` 下的层级更接近
    - `scsi_host_template` 也新增了最小 `target_alloc/target_destroy` 钩子，`virtio_scsi.c` 现在把 target 私有 `hostdata` 绑定到 `scsi_target`，而不是继续把这类状态塞在 host 或 device 层
    - 当 `lun` 是 wild-card 时，Lite 会先探测 `lun 0`；只有目标有响应时，才继续发 `REPORT LUNS` 扩展该 target 的 LUN 边界；若 `REPORT LUNS` 不支持，则回退到最小顺序 LUN 扫描（`lun 1..7`，遇到首个 miss 即停止）
    - Lite 会在 `scsi_target` 上缓存 “REPORT LUNS 不支持” 状态（基于 `CHECK CONDITION + ILLEGAL REQUEST + ASC(0x20/0x24)`），后续扫描该 target 时直接跳过 `REPORT LUNS`，避免反复慢扫
    - Lite 的 `REPORT LUNS` 初始分配长度也收敛到和 Linux 一样的 511 项上限，并在响应声明更长列表时按返回长度重试分配
    - 这使 Lite 的 target 边界不再由 `virtio_scsi.c` 硬编码为 `target 0`，而是回到 `shost->max_id` 所表达的 host 边界；同时 `target` 也已经有了最小对象承载，但仍未实现 Linux 那套完整 `scsi_target` 状态位、引用计数与传输层 target 发现
    - 只有探测成功后，才正式 `scsi_add_device()`、`scsi_add_disk()` 并落成 `gendisk`，于是出现 `/dev/sda`
    - 已发现盘保存在 `Scsi_Host` 私有列表里，失败 LUN 不进入 device model
4.  与 Linux 的差异：
    - Lite 目前虽然已经能保留多块已发现 disk，但仍没有完整 SCSI midlayer、异步扫描、热插拔重扫、error handling 和 task management
    - Linux `virtio_scsi` 是设置 `shost->max_*` 后直接 `scsi_scan_host(shost)`，并通过 `target_alloc/target_destroy` 挂载 `virtio_scsi_target_state`；Lite 现在也已经收敛到这个入口，并补了最小 `target_alloc/target_destroy` 与 target `hostdata` 绑定，同时保留“先 `lun 0`、再 `REPORT LUNS`、失败后最小顺序 LUN 扫描”的 target 扫描顺序；但仍缺完整 `scsi_target` 生命周期、异步扫描与更丰富的 LUN addressing 支持，因此属于简化版 Linux 语义
    - virtio 目前只走 legacy/transitional PCI I/O port 路径（仍属 `virtio-pci` transport 的一种），没有实现 modern PCI capability 模型；但前端 `virtio_scsi.c` 已不再直接读写 `VIRTIO_PCI_*` 寄存器，队列创建与 notify 均下沉到 transport/virtqueue 层
    - virtio/NVMe/PCI/PCIe/Block 等子系统的 Linux 2.6 对齐差异与计划，统一收录在 [Linux26-Subsystem-Alignment.md](Linux26-Subsystem-Alignment.md)

### Q8.10: 为什么 `virtio-scsi` 第一次 `TEST UNIT READY` 可能失败，但后续还能正常工作？
**回答**：
1.  这是标准 SCSI 现象：设备上电或总线 reset 后，第一次 `TEST UNIT READY` 常返回 `CHECK CONDITION`，sense key 常见为 `UNIT ATTENTION`。
2.  本次运行时证据里，第一次 TUR 返回了 `key=6, asc/ascq=41/00`，含义是 `POWER ON, RESET, OR BUS DEVICE RESET OCCURRED`。
3.  这种情况下最小正确做法通常是重试一次或几次 TUR，再继续 `INQUIRY/READ CAPACITY`；Lite 当前 `virtio-scsi` 就采用了这个最小重试策略。

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
