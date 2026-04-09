# Lite Kernel 源码深度解析

本文档旨在对 `lite` 内核项目中的每一个源文件、核心函数进行详细的剖析，帮助学习者理解从引导到内核主循环、以及中断处理和驱动的完整生命周期。

---

## 1. 引导与入口

### `boot.s`
这是整个内核的真正入口点，负责定义 Multiboot 头部，并为 C 语言内核准备初始环境（栈）。

*   **Multiboot Header (`.section .multiboot`)**
    *   定义了 `MAGIC` (`0x1BADB002`)，告诉 GRUB 等引导加载器这是一个合法的 Multiboot 内核。
    *   定义了 `FLAGS`，要求引导加载器提供内存映射 (`MEMINFO`) 和按页对齐 (`ALIGN`)。
    *   定义了 `CHECKSUM`，以通过 GRUB 的合法性校验。
*   **栈空间分配 (`.section .bss`)**
    *   通过 `.skip 16384` 分配了 16 KiB 的未初始化内存作为内核初始栈。
    *   `stack_top` 标签标记了栈顶地址（栈在 x86 下是向下生长的）。
*   **入口函数 `_start` (`.section .text`)**
    *   这是 GRUB 加载内核后跳转执行的第一条指令。
    *   `mov $stack_top, %esp`：设置栈指针，至此可以安全地调用 C 函数。
    *   `call kernel_main`：跳转到 C 语言的主函数。
    *   `cli; 1: hlt; jmp 1b`：如果 `kernel_main` 意外返回，则关闭中断并进入死循环，挂起 CPU。

---

## 2. 全局描述符表 (GDT)

### `asm/gdt.h` & `gdt.c`
GDT 用于定义内存的分段机制。虽然在平坦模式下，所有段都指向 0 到 4GB 的全量空间，但 x86 架构仍强制要求 GDT 存在以定义代码执行权限（Ring 0 / Ring 3）。

*   **`struct gdt_entry`**：GDT 表项结构，包含段基址（32位）、段界限（20位）、访问权限和粒度标志。
*   **`struct gdt_ptr`**：用于传递给 `lgdt` 指令的指针结构，包含 GDT 的界限（大小）和基址。
*   **`gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)`**
    *   辅助函数，通过位运算将基址和界限拆分填入 GDT 表项的各个碎片字段中。
*   **`init_gdt(void)`**
    *   初始化 6 个段：Null 段（必须为 0）、内核代码段、内核数据段、用户代码段、用户数据段、TSS 段。
    *   所有有效段的基址均为 `0`，界限均为 `0xFFFFFFFF`（4GB）。
    *   内核段权限包含 `Ring 0`，用户段权限包含 `Ring 3`。
    *   最后调用 `gdt_flush` 并加载 TSS（`ltr`）。

### `gdt_flush.s`
*   **`gdt_flush`**
    *   **参数传递**：`init_gdt()` 会把 `&gdt_ptr` 作为参数传入 `gdt_flush`（cdecl 调用约定）。
        *   `mov 4(%esp), %eax`：从栈上取出第 1 个参数到 `eax`。其中 `0(%esp)` 是返回地址，`4(%esp)` 才是参数。
    *   **`lgdt` 指令**：`lgdt (%eax)` 把 `%eax` 指向的“GDT 指针结构”加载到 CPU 的 `GDTR` 寄存器。
        *   `GDTR` 保存两部分信息：`limit`（GDT 总大小 - 1）和 `base`（GDT 表的线性地址）。
        *   加载 `GDTR` 只是在告诉 CPU “GDT 在哪里”，并不会自动刷新现有段寄存器的缓存描述符。
    *   **为什么要重载段寄存器**：CPU 的段寄存器（`cs/ds/es/fs/gs/ss`）内部会缓存对应描述符的属性。换了 GDT 之后，如果不重载，可能继续使用旧缓存，导致权限/类型检查异常。
        *   `mov $0x10, %ax`：把选择子 `0x10` 写入 `ax`，随后写入 `ds/es/fs/gs/ss`。
        *   `0x10` 的来源：选择子值通常等于 `Index * 8`。内核数据段在 GDT 的 Index=2，所以 `2 * 8 = 16 = 0x10`。
    *   **为什么要用 `ljmp` 刷新 `cs`**：`cs` 不能用 `mov` 直接赋值，必须通过 far jump/far call/iret 等方式更新。
        *   `ljmp $0x08, $.flush`：把 `cs` 切到选择子 `0x08`（Index=1 的内核代码段），并跳转到 `.flush` 标签继续执行。
        *   `.flush: ret`：返回到 `init_gdt()`，后续代码就运行在新的 GDT 与新的段寄存器上下文中。

---

## 3. 中断描述符表 (IDT)

### `asm/idt.h` & `idt.c`
IDT 告诉 CPU 在发生特定硬件中断或软件异常时，应该跳转到哪段代码执行。

*   **`struct idt_entry_struct`**：IDT 表项，包含处理函数的地址（基址）、段选择子以及门标志。
*   **`idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)`**
    *   将传入的中断号 `num` 对应的 IDT 表项配置为指向 `base` 地址的函数，并设置权限（如 `0x8E` 表示 Ring 0 的中断门）。
*   **`init_idt(void)`**
    *   将 256 个 IDT 表项清零，设置 IDT 指针，并调用 `idt_flush` 加载到 CPU。

### `idt_flush.s`
*   **`idt_flush`**
    *   `lidt (%eax)`：将 IDT 的基址和界限加载到 IDTR 寄存器中。

---

## 4. 中断服务例程 (ISR & IRQ)

### `linux/interrupt.h` & `isr.c`
这是中断处理的高级 C 语言层。

*   **`struct pt_regs`**：定义了当中断发生时，CPU 和汇编桩压入栈的寄存器结构，便于 C 函数读取现场状态。
*   **`pic_remap(void)`**
    *   可编程中断控制器 (PIC) 初始化。
    *   **关键作用**：BIOS 默认将 IRQ 0-7 映射到中断 8-15，但这与 x86 CPU 保留的异常（如 Double Fault）冲突。此函数向 PIC 发送 ICW 命令，将主片 IRQ 映射到 32-39，从片映射到 40-47。
    *   `outb(0x21, 0xFC)`：修改主片掩码，**仅开启 IRQ0（时钟）和 IRQ1（键盘）**。
*   **`isr_install(void)`**
    *   调用 `idt_set_gate` 将 0 到 31 号异常的汇编处理入口（`isr0` - `isr31`）注册到 IDT。
*   **`irq_install(void)`**
    *   调用 `pic_remap` 初始化硬件中断。
    *   将 IRQ0 和 IRQ1 的汇编处理入口注册到 IDT 的 32 和 33 号向量。
*   **`isr_handler(struct pt_regs *regs)`**
    *   **CPU 异常总入口**。如果发生严重错误（如除零、缺页），会打印红色的 `KERNEL PANIC!` 以及具体的异常名称（通过 `exception_messages` 数组查询），然后进入死循环（Halt）。
*   **`irq_handler(struct pt_regs *regs)`**
    *   **硬件中断总入口**。
    *   **关键步骤**：向 PIC 发送 EOI（End Of Interrupt，`0x20`），告诉硬件“中断已处理”，否则硬件不会发送下一个中断。
    *   如果有注册对应的驱动回调函数（如键盘），则调用之。

### `interrupt.s`
这是中断处理的底层汇编桩。

*   **`ISR_NOERRCODE` / `ISR_ERRCODE` 宏**
    *   用于批量生成 0-31 号异常的入口点。
    *   有些异常 CPU 会自动压入错误码，有些不会（宏中手动 `push $0` 补齐，保证栈结构一致）。
    *   压入中断号，然后跳转到 `isr_common_stub`。
*   **`isr_common_stub` / `irq_common_stub`**
    *   **保存现场**：`pusha` 保存通用寄存器。
    *   **切换段寄存器**：保存原数据段，加载内核数据段 (`0x10`)。
    *   **传递参数**：`push %esp`，将当前栈指针（即 `struct pt_regs` 结构体的起始地址）作为参数传给 C 函数。
    *   **调用 C 函数**：`call isr_handler` 或 `call irq_handler`。
    *   **恢复现场**：恢复数据段寄存器，`popa` 恢复通用寄存器。
    *   **返回**：清理错误码和中断号，执行 `iret` 从中断返回。

---

## 5. 键盘驱动

### `keyboard.c`
基于 PS/2 控制器的基础键盘驱动。

*   **`kbdus` 数组**：一个包含 256 个元素的查找表，将键盘硬件发出的扫描码 (Scancode) 映射为对应的 ASCII 字符。
*   **`keyboard_callback(struct pt_regs *regs)`**
    *   中断回调函数。当用户按键时被 `irq_handler` 触发。
    *   `inb(0x60)`：从键盘数据端口读取扫描码。
    *   **断码判断**：如果扫描码最高位为 1 (`scancode & 0x80`)，表示按键释放，当前逻辑选择忽略。
    *   **通码处理**：对于按键按下，通过 `kbdus` 数组查找字符并调用 `vga_put_char` 显示在屏幕上。
*   **`init_keyboard(void)`**
    *   注册中断回调。
    *   清理 PS/2 控制器的残留缓冲区。
    *   读取并修改 PS/2 命令字节，强制使能 IRQ1 中断。
    *   发送 `0xF4` 命令允许键盘开始扫描。

---

## 6. 内核主逻辑

### `kernel.c`
串联所有模块的核心。

*   **VGA 文本模式驱动**：
    *   `init_vga`：清空屏幕。
    *   `terminal_scroll`：**滚屏功能**。当输出到达屏幕底部（第 25 行）时，将第 1-24 行的数据拷贝到 0-23 行，并清空最后一行，防止内容被覆盖。
    *   `vga_put_char`：处理字符输出。新增对退格键 `\b` 的支持（光标左移并写入空格覆盖），直接向物理内存地址 `0xB8000`（VGA 显存）写入带有颜色属性的字符数据。
    *   `strcmp` / `strncmp`：由于缺乏 C 标准库 (libc)，内核需自己实现基础的字符串比较函数，为 Shell 的命令解析提供支持（已重构至 `libc.c`）。

---

## 7. 基础模块：定时器与 C 库

### `linux/timer.h` & `drivers/clocksource/timer.c`
可编程间隔定时器（PIT, Programmable Interval Timer）是操作系统感知时间、进行进程调度的基础。
*   **硬件原理**：PIT 有一个固定频率的内部时钟（1193180 Hz）。通过向其端口发送除数（Divisor），可以控制其产生 IRQ0 中断的频率。
*   **`init_timer(uint32_t frequency)`**
    *   注册 `timer_callback` 到 IRQ0。
    *   计算除数：`1193180 / frequency`（本内核设为 100Hz，即每 10 毫秒触发一次中断）。
    *   向 PIT 命令端口 `0x43` 写入 `0x36`（通道 0，先低后高，Rate Generator 模式）。
    *   将除数拆分为高低两字节，发送到 PIT 端口 `0x40`。
*   **时间追踪**：每次中断发生时，全局变量 `tick` 加 1。通过 `tick / frequency` 即可计算出系统运行的秒数 (`uptime`)。

### `linux/libc.h` & `lib/libc.c`
由于裸机环境下没有标准的 `<stdio.h>` 或 `<string.h>`，内核必须自带一套极简的 C 标准库。
*   **内存操作**：`memset`, `memcpy`，用于后续内存分配器的初始化与拷贝。
*   **字符串操作**：`strlen`, `strcmp`, `strncmp`。
*   **格式化输出**：
    *   `itoa`：核心转换函数，利用取模运算将整数（十进制或十六进制）转换为 ASCII 字符串。
    *   `printf`：基于可变参数宏（`stdarg.h`）实现的极简版打印函数，目前支持 `%d`（整数）、`%x`（十六进制）、`%s`（字符串）、`%c`（字符），极大方便了后续内存地址的调试与输出。

---

## 8. 物理页分配 (page_alloc)

### `asm/multiboot.h` & `page_alloc.c`
这是操作系统接管物理硬件资源的第一步。
*   **Multiboot 协议**：GRUB 在加载内核时，会把一张“系统地图”放在内存里，并通过寄存器 `%ebx` 告诉内核这张地图在哪。
*   **`kernel_entry` (中间层)**：我们在 `boot.s` 和 `start_kernel` 之间加了一个跳板，负责把汇编里的寄存器参数转换成 C 语言的函数参数。
*   **`free_area_init`**：读取 Multiboot 结构体，获取 `mem_lower` (低端内存) 和 `mem_upper` (高端内存) 的大小。
    *   **详细流程**：
        1.  统计总内存容量，并换算为物理页数量（每页 4KB）。
        2.  由 `bootmem_alloc` 分配 buddy 元数据（`buddy_next`），并按页对齐保留。
        3.  `struct page` 作为主状态源，默认标记 `PG_RESERVED`。
        4.  遍历 BIOS E820 内存地图，对可用区域清除 `PG_RESERVED`，同时跳过 bootmem 保留区。
*   **`show_mem`**：遍历 BIOS 提供的 E820 内存地图。这张表详细列出了哪些物理地址是 `AVAILABLE` (可用 RAM)，哪些是 `RESERVED` (被硬件占用)。这是后续编写“物理页分配器”的唯一依据。
*   **Buddy 分配器 (page_alloc)**：
    *   **原理**：通过 `struct page` 与 `free_area` 维护空闲块，分配路径统一进入 buddy。
*   **当前内存布局（可扩展）**：
    *   **低端内存 (0 ~ 1MB)**：BIOS 数据区、IVT、VGA 显存、Multiboot 结构体等，保持保留不分配。
    *   **内核镜像**：从 `0x100000` 开始，依次是 `.text/.rodata/.data/.bss/.bootstrap_stack`，由链接脚本布局。
    *   **模块区 (可变)**：Multiboot 模块结构体数组 + 模块数据（InitRD 以及后续新增模块），大小随模块数量变化。
*   **位图**：放在“内核镜像 + 模块区”之后，并对齐到页边界。
    *   **可用物理页区**：位图之后的可用 RAM，由 E820 标记为 `AVAILABLE` 的区域逐页释放。
    *   **扩展性**：如果未来增加更多模块，它们会占用模块区并向上增长，page_alloc 位图总是移动到模块区之后，确保不会被覆盖。

---

## 9. 虚拟内存管理 (pgtable) 与分页

### `asm/pgtable.h` & `memory.c`
开启分页 (Paging) 是操作系统进入保护模式完全体的重要标志。它引入了虚拟地址到物理地址的映射机制。

*   **页目录 (PDE) 与页表 (PTE)**：
    *   采用 x86 标准的两级分页结构。每个表项 4 字节，包含物理基址和属性位（Present, Read/Write, User 等）。
*   **恒等映射 (Identity Mapping)**：
    *   在开启分页的瞬间，如果 CPU 的指令指针 (EIP) 指向的虚拟地址没有被映射，系统会立刻崩溃（Triple Fault）。
    *   因此，我们首先将物理地址 `0x000000 - 0x400000` (前 4MB) 映射到虚拟地址的 `0x000000 - 0x400000`。
*   **`paging_init`**：
    1.  申请页目录（4KB 对齐）并清零，确保所有 PDE 初始为 not-present。
    2.  申请第一个页表，用于映射低端内存（至少覆盖 0~4MB）。
    3.  扩展恒等映射到 0~128MB：共 32 个页表，每张表映射 4MB，确保内核、InitRD 和低端内存都可访问。
    4.  将每张页表挂入页目录（PDE 标记为 Present/RW），并填充 1024 个 PTE（每页 4KB）。
    5.  注册缺页异常 (#PF, Interrupt 14) 处理函数，避免开启分页后缺页直接 Triple Fault。
    6.  加载 `CR3` 指向页目录地址，再置位 `CR0.PG` 开启分页。
    7.  开启后所有地址走页表翻译，恒等映射保证当前 EIP 仍可取指执行。
*   **`map_page`**：
    *   作用：建立“物理页 → 虚拟页”的映射，必要时自动分配新的页表并写入页目录。
    *   入参来源：
        *   `phys_addr` 通常来自 `alloc_page(GFP_KERNEL)` 或 Bootloader 提供的模块物理地址（如 InitRD）。
        *   `virt_addr` 由内核虚拟地址布局策略决定（当前多为恒等映射，后续可扩展高地址映射）。
    *   关键步骤：
        *   计算 PDE/PTE 下标定位页表项。
        *   若对应 PDE 不存在，分配新页表并刷新 CR3 以清空 TLB。
        *   写入 PTE 后用 `invlpg` 刷新该页的 TLB。
*   **Paging 查询接口**：
    *   `page_mapped`：判断某个虚拟地址是否已建立 PTE 映射。
    *   `virt_to_phys`：将已映射的虚拟地址转换为物理地址（包含页内偏移）。
*   **mm 结构**：
    *   每个任务持有独立的 `mm`，包含 `pgd` 与 `mmap/start_brk/brk/start_stack` 等地址空间元信息，用于缺页校验与回收。
    *   内核线程不持有用户 `mm`，调度切换时会显式切回 `kernel_directory`，避免在用户页表中运行内核线程。
*   **`do_page_fault`**：
    *   当访问非法内存地址时触发（#PF，Interrupt 14）。
    *   从 `CR2` 寄存器读取导致错误的线性地址（faulting address）。
    *   解析错误码（Error Code），区分：
        *   是否存在 (Present/Not Present)
        *   读/写访问
        *   用户态/内核态
        *   保留位错误或取指错误
    *   当前实现支持最小按需映射：
        *   对 not-present 且非 reserved 的缺页，分配物理页并映射到 fault 地址所在页，清零后返回继续执行。
    *   对 present 但非用户态权限的缺页，如果 VMA 允许访问，将该页修正为用户页并按需设置读写权限。
    *   该修正用于兼容低端恒等映射区内的用户地址（避免 “present but no user permission” 类缺页）。
        *   对低地址（< 0x1000）与 reserved/权限类错误仍直接 panic。
    *   COW：当写入触发 present fault 且 PTE 标记为 COW 时，分配新页并复制，或在引用计数为 1 时直接升级为可写。
    *   fork 后父子首次写入同一页会各触发一次 COW 缺页，日志中的 `Page Fault handled` 属预期行为。
*   **`isr_handler` / `irq_handler` 调用流程**：
    *   **异常路径（ISR）**：
        *   `isrX` 汇编入口压入错误码/中断号，跳转到 `isr_common_stub`。
        *   `isr_common_stub` 保存现场、切换段寄存器，把 `struct pt_regs*` 传给 `isr_handler`。
        *   `isr_handler` 若找到注册回调则调用，否则打印异常并 panic。
    *   **硬件中断路径（IRQ）**：
        *   `irqX` 汇编入口压入伪错误码/中断号，跳转到 `irq_common_stub`。
        *   `irq_common_stub` 保存现场并调用 `irq_handler`。
        *   `irq_handler` 先发 EOI，再调用对应回调或专用处理（如串口 IRQ4）。

---

## 10. 文件系统与 Initramfs

### `init/initramfs.c`
为了让内核在早期就拥有可用的用户态镜像，Lite 使用 **initramfs（cpio newc）**：引导加载器（QEMU/GRUB）把 cpio 归档作为 Multiboot 模块加载到内存，内核在挂载 `rootfs`（ramfs）后，逐条解析 cpio 记录并在 `/` 下创建目录与文件节点。

*   **Multiboot 模块**：
    *   `struct multiboot_info` 的 `mods_addr/mods_count` 指向模块数组（`struct multiboot_module`），通过 `mod_start/mod_end` 获取镜像所在的物理地址范围。
*   **初始化流程（start_kernel）**：
    *   先完成早期内存与分配器初始化（bootmem/zone/page_alloc/paging/slab 等），保证模块内存可读。
    *   `vfs_init()` 挂载 `rootfs`（ramfs）成为 `/`。
    *   调用 `populate_rootfs()` 将 initramfs 解包到 `/`。
*   **解包语义**：
    *   格式为 cpio newc，按 `c_namesize/c_filesize/c_mode` 解析。
    *   遇到目录记录时创建目录；遇到普通文件记录时创建文件并写入内容。
    *   文件名为 `TRAILER!!!` 表示结束。

---

## 11. 串口驱动与早期控制台 (Early Console)

### `serial.h` & `serial.c`
为了在没有图形界面的情况下也能方便地调试内核（或者通过 `qemu -serial stdio` 输出），我们实现了一个极简的串口驱动（COM1）。

这里的核心设计思想是**分离早期控制台与完整驱动**：
1. **Early Console**：在 `start_kernel` 最早期调用的 `init_serial` 仅仅设置波特率并关闭该端口的硬件中断，它通过**轮询（Polling）**的方式进行输出。这样即使在 IDT 尚未建立、中断被屏蔽（`cli`）的极端早期阶段，内核依然可以安全地使用 `printf` 打印调试日志。
2. **Full Driver**：真正的中断注册（如 `IRQ4`）被延迟到了 `serial_driver_init` 中，它被 `module_init` 宏标记，在系统基础架构完全就绪后，由 PID=1 的内核线程在 `do_initcalls` 阶段统一安全加载。

---

## 调试经验总结

在开发 initramfs 与串口交互的过程中，我们遇到了两个典型问题，详细记录在 [Issues.md](file:///data25/lidg/lite/Documentation/Issues.md) 中：
1.  **initramfs Triple Fault**：由于错误解引用 Multiboot 结构体指针，导致访问无效地址 `0xFFFFFFFF`，触发缺页异常并升级为 Triple Fault。解决方法是修正指针算术并确保页面分配器/页表初始化正确映射了模块内存区域。
2.  **Shell 无响应**：在 QEMU `-nographic` 模式下，输入来自串口而非键盘。必须实现串口中断处理 (IRQ 4) 才能接收输入。同时需要处理 `\r` 与 `\n` 的兼容性问题。

面向后续 Linux-like 方向的最新路线图见 [roadmap_v4.md](file:///data25/lidg/lite/Documentation/roadmap_v4.md)。

## 12. 内核堆分配器 (Kernel Heap)

### `linux/slab.h` & `slab.c`
为了在内核中支持动态数据结构（如链表、树），我们需要实现 `kmalloc` 和 `kfree`。我们采用了一个简单的**链表式分配器 (Linked List Allocator)**。

*   **数据结构**：
    *   每个内存块由一个 `struct header` 头部管理，包含 `size` (大小)、`is_free` (是否空闲) 和 `next` (下一个块指针)。
    *   整个堆本质上是一个由 `struct header` 串联起来的单向链表。
*   **初始化**：
    *   堆的虚拟起始地址固定为 `PAGE_OFFSET` (3GB 处)。
    *   初始大小为 **1MB**（`KHEAP_INITIAL_SIZE = 0x100000`），虚拟范围为 `PAGE_OFFSET ~ PAGE_OFFSET+1MB`。
    *   初始化时，调用 `kheap_extend` 申请物理页并映射到该虚拟地址，创建一个覆盖整个区域的巨大空闲块。
*   **`kmalloc(size)`**：
    *   **First Fit 策略**：遍历链表，找到第一个大小满足要求的空闲块。
    *   **Split (分裂)**：如果找到的块远大于需求，将其切割成“已用块”和“新空闲块”，将剩余空间归还给堆。
*   **`kfree(ptr)`**：
    *   根据指针找到头部，标记为 `is_free = 1`。
    *   **Coalesce (合并)**：检查下一个块是否也是空闲的，如果是，则合并成一个大块，减少内存碎片。
*   **自动扩展（当前已接入）**：
    *   当 `kmalloc` 找不到可用块时，会调用 `kheap_extend` 映射新页并追加空闲块，随后重试分配。

---

## 13. 交互式 Shell

### `shell.h` & `shell.c`
这是一个运行在内核态（Ring 0）的极简命令行解释器，它赋予了系统与用户交互的能力。
> **注意**：在标准的现代操作系统中，Shell（如 bash）是运行在用户态的普通应用程序，通过系统调用与内核通信。由于我们目前还没有实现完整的用户态运行时和文件系统接口，因此暂时将这个极简 Shell 编译进内核，并以“内核任务”的形式运行。
*   **输入模型**：
    *   键盘 IRQ1 与串口 IRQ4 的中断回调只负责将字符入队到输入队列（不在中断上下文做命令解析）。
    *   `shell_task` 在任务上下文中阻塞式取字符，处理回显、退格、回车，并在回车时调用 `shell_execute` 执行命令。
*   **命令分发**：
    *   通过 `strcmp/strncmp` 做硬编码匹配，直接调用内核功能函数。
    *   典型命令包括 `meminfo`、`uptime`、`alloc`、`vmmtest`、`heaptest`、`ls`、`cat` 等。
*   **输入来源统一**：
    *   键盘 IRQ1 与串口 IRQ4 最终都会把字符送到 Shell 处理流程中。
*   **调度演示开关**：
    *   `demo on` / `demo off` 控制内核线程演示输出，`demo` 查询当前状态。
*   **任务调度控制**：
    *   `yield` 触发一次协作式让出。
    *   `sleep` 让当前任务休眠一段 tick 后再恢复运行。
*   **用户态自举入口**：
    *   内核启动后会自动创建 `init.elf`（PID1），由 init 负责 `exec` 用户态 shell（如 `shell.elf`）。
    *   若 init 不存在，则退回内核态 shell 作为调试控制台。
    *   控制台最小 tty 行规程由内核提供：回显与 canonical 输入缓冲由 `/dev/console` 统一处理。
    *   `SYS_IOCTL` 可读取/设置 console 的 tty flags（回显/规范模式）。
    *   Ctrl-C 在控制台层被识别为 SIGINT，用于中断前台用户任务并返回内核提示符。
    *   `mmap.elf` 用于验证匿名 mmap/munmap：运行后会打印 `/proc/self/maps` 以对齐 VMA 与缺页行为。
    *   用户态 `ush` 的 `run <file>` 使用 `execve` 替换当前进程，不会返回原 shell。
    *   时钟中断会驱动时间片递减，时间片耗尽后触发一次调度切换（避免每个 tick 都强制切换）。
*   **任务状态查看**：
    *   `ps` 输出任务列表、状态与唤醒 tick。
    *   状态包含 `RUNNABLE/SLEEPING/BLOCKED/ZOMBIE`，用于定位调度与等待关系。
*   **procfs 可观测接口**：
    *   `ls proc` 可列出 procfs 文件节点，`cat proc/tasks|proc/sched|proc/irq|proc/maps` 可读取调度/中断/地址空间信息。
    *   `/proc/self/maps` 与 `/proc/<pid>/maps` 提供按 pid 维度的 VMA 视图，用于对齐后续 `/proc/<pid>/*` 的扩展方向。
    *   `/proc/meminfo` 提供物理内存统计，`/proc/<pid>/stat` 提供任务基础状态。
    *   `/proc/<pid>/cmdline`、`/proc/<pid>/status`、`/proc/<pid>/cwd`、`/proc/<pid>/fd/*` 用于观测任务、cwd 与 fdtable。
*   **sysfs 最小自描述接口**：
    *   `/sys/kernel/version`、`/sys/kernel/uptime` 与 `/sys/devices/*` 用于提供内核与设备的基础属性视图。
*   **设备模型（kobject/device/driver/bus）**：
    *   引入最小 kobject 与 platform bus，设备注册后可通过 `/sys/devices/<dev>` 观测。
    *   设备目录提供 `type/bus/driver` 三个只读文件；未绑定 driver 时显示 `unbound`。
    *   当前默认注册 console/ramfs，并用同名 driver 自动绑定用于验证 probe/bind 框架。
*   **VFS（对象化进行中）**：
    *   目前引入了最小 mount 表与 `super_block/inode/dentry/file` 结构雏形，并在启动时把 `/`、`/proc`、`/dev`、`/sys` 加入 mount 表用于路径解析与根目录挂载点展示。
    *   inode/dentry 引入最小缓存与引用计数，避免重复分配对象并为后续生命周期管理铺路。
    *   目录遍历接口开始收敛为 getdents 语义，用户态 shell 使用批量读取目录项（linux_dirent 风格）并在用户态解析输出。
    *   引入 uid/gid/mode/umask 的最小权限闭环：mkdir/open/read/write/chdir 会做权限判断。
    *   cwd 为 per-task，相对路径会基于当前任务 cwd 归一化，并支持 `.`/`..`。
    *   `/` 指向 ramfs 并可写；initramfs 在启动早期解包到 `/`，提供 `/sbin/init`、`/sbin/sh`、`/bin/*` 等用户态镜像。
*   **系统调用最小闭环**：
    *   `syscall` 使用 `int 0x80` 调用 `SYS_WRITE/SYS_YIELD` 进行测试。
    *   用户指针校验仅对 Ring3 生效，避免内核态演示传入内核指针被拒绝。
    *   `SYS_WRITE/SYS_READ` 在用户态使用 `copyin/copyout` 分段拷贝，避免直接解引用用户指针。
    *   `SYS_READ` 从 shell 输入读取时采用阻塞等待，不再通过循环 `yield` 轮询。
    *   `SYS_BRK` 用于调整用户堆顶（brk），堆 VMA 会随着 brk 扩展，物理页由缺页按需分配。
    *   通过 `SYS_OPEN/SYS_READ/SYS_WRITE/SYS_CLOSE` 提供最小 fd 风格 I/O 能力（fd=0/1/2 绑定 `/dev/console`），fdtable 为每任务独立且 fd 持有 file 对象与 offset。
    *   为支持用户态 shell，引入 `SYS_CHDIR/SYS_GETCWD/SYS_GETDENT/SYS_GETDENTS/SYS_MKDIR`，使目录遍历与路径切换不依赖内核态命令。
    *   引入 `SYS_GETUID/SYS_GETGID/SYS_UMASK/SYS_CHMOD` 形成最小权限接口闭环。
    *   为支持用户态自举，引入 `SYS_EXECVE/SYS_WAITPID`：exec 用于替换当前用户进程映像，waitpid 用于等待子进程退出并回收资源。
    *   `SYS_IOCTL` 作为设备控制入口，目前仅对 `/dev/console` 提供最小响应，后续将挂接 tty/ioctl 语义。
    *   `SYS_KILL` 作为最小信号投递接口，当前支持 SIGINT 中断前台任务。
    *   `SYS_MMAP/SYS_MUNMAP` 提供匿名映射与回收，映射区由 VMA 记录并在 `/proc/<pid>/maps` 可观测。
    *   `SYS_FORK` 提供最小 fork，配合 COW 页与引用计数实现写时复制。
    *   `int 0x80` 的入口使用 trap gate，且 syscall 汇编入口不会隐式 `cli`，为内核态可抢占打基础。
*   **用户态程序加载**：
    *   用户态执行基于 VFS 路径解析与打开读取（默认会尝试 `/bin/<name>`），再按 ELF PT_LOAD 段映射到用户空间并进入执行。
    *   `user` 是 `run user.elf` 的快捷命令。
    *   用户程序可通过 `SYS_EXIT` 退出，默认行为为交互式回显，`q` 退出，`!` 触发越界缺页，`b` 触发坏指针写测试。
    *   `cat.elf` 作为用户态文件访问示例：运行 `run cat.elf` 会读取并打印 `readme.txt`。
    *   加载器会基于 ELF 段权限（`p_flags`）设置 VMA 标志，并在加载完成后将非写段页设置为只读。
    *   纯 BSS 段允许 `p_filesz=0`，不再因 `p_offset` 超出文件大小误判越界。
    *   程序路径解析基于 VFS mount 表（例如 `/proc` `/dev` `/sys` 的路径会被正确路由到对应文件系统）。
    *   进入用户态后切换输入前台为 `user>`，用户任务退出后自动恢复 `lite-os>`。
*   **用户态异常策略**：
    *   用户态触发 CPU 异常（如 `#GP/#UD`）或不可恢复的 `#PF` 时，不再触发内核 `panic`，而是结束当前用户任务并返回 shell。
*   **用户进程退出回收**：
    *   用户任务退出后进入 `ZOMBIE` 状态，记录退出码与退出原因，等待 `sys_waitpid(pid, ...)` 回收资源。
    *   `sys_waitpid` 会使调用者进入 `BLOCKED` 状态，直到目标任务退出事件发生再被唤醒。
*   **kthread 与 mm 语义**：
    *   内核线程不再分配 user mm（mm 为空）；只有用户任务持有独立 mm 与用户页表。
    *   目的：避免地址空间归属歧义，降低后续 `fork/COW`、`/proc/<pid>/maps` 等扩展的复杂度。
    *   shell 的 `run` 命令会等待用户任务结束，并打印 `exit: code=... reason=... info0/info1` 用于定位异常退出（如缺页地址与 EIP）。
    *   回收路径以 VMA 为唯一语义源：遍历 VMA 范围释放已映射用户页，并释放非内核共享的页表页与页目录页，避免 heap/brk 扩展产生泄漏。
*   **等待队列 (waitqueue)**：
    *   内核提供最小等待队列抽象：等待者进入 `BLOCKED` 并挂到队列上，事件发生时唤醒等待者恢复为 `RUNNABLE`。
    *   当前用于：`sys_waitpid` 等待用户任务退出事件，以及 shell 输入到达时唤醒阻塞读者。
*   **独立页表注意点**：
    *   用户态访问的页表项与页目录项必须同时设置 `PTE_USER`，否则会触发用户态缺页异常。
*   **TSS 与内核栈切换**：
    *   x86 从 Ring3 陷入 Ring0（中断/异常/syscall）需要通过 TSS 的 `esp0/ss0` 切换到内核栈。
    *   内核在任务切换时会更新 `tss_set_kernel_stack()`，确保每个任务陷入内核时使用当前任务的内核栈顶。

*   **命令缓冲区 (`cmd_buffer`)**：一个 256 字节的全局数组，用于暂存用户输入的字符，直到按下回车键。
*   **`shell_process_char(char c)`**
    *   **接收输入**：由 `keyboard.c` 的键盘中断回调触发。
    *   **退格处理**：如果收到 `\b` 且缓冲区不为空，则将索引减 1，并向屏幕发送退格符擦除字符。
    *   **执行处理**：如果收到 `\n`（回车），则在屏幕换行，并调用 `shell_execute()` 解析命令。
    *   **常规字符**：将字符存入缓冲区，并回显到屏幕。
*   **`shell_execute(void)`**
    *   **字符串封口**：在缓冲区末尾追加 `\0`，将其转换为合法的 C 字符串。
    *   **硬编码解析**：通过一系列 `strcmp` 和 `strncmp` 判断用户输入。
        *   `help`：打印支持的命令。
        *   `clear`：调用 `init_vga` 清屏。
        *   `info`：打印内核的架构和特性信息。
        *   `echo`：使用 `strncmp` 匹配前缀，并打印参数部分。
    *   **重置缓冲区**：执行完毕后，将 `cmd_index` 归零，准备迎接下一条命令。
*   **`kernel_main(void)`**
    *   按顺序执行系统初始化：
        1.  `init_vga()`：屏幕就绪。
        2.  `init_gdt()`：内存分段就绪。
        3.  `init_idt()`：中断向量表就绪。
        4.  `isr_install()`：CPU 异常防线就绪。
        5.  `irq_install()`：硬件中断控制器就绪。
        6.  `init_keyboard()`：键盘驱动就绪。
        7.  `__asm__ volatile ("sti")`：**打开全局中断**，允许 CPU 接收硬件信号。
    *   输出启动成功信息。
    *   **内核主循环**：`while(1) { __asm__ volatile("hlt"); }`。使 CPU 进入低功耗等待状态，直到被键盘等中断唤醒，处理完中断后再次回到睡眠状态。保证内核不退出。

## 当前最小功能清单
- Multiboot 协议兼容的内核入口
- 文本输出（VGA 与串口，支持滚屏与退格）
- 自定义 GDT 与段寄存器初始化
- IDT 与 CPU 异常捕获（0-31）
- PIC 初始化与硬件中断（IRQ）映射
- PS/2 键盘中断驱动与基础字符输入
- **PIT 时钟驱动与系统滴答 (Tick)**
- **极简标准 C 库实现 (libc)**
- **物理页分配 (page_alloc)**：解析 Multiboot 内存地图，使用**位图 (Bitmap)** 管理物理页分配与释放。
- **虚拟内存管理 (paging)**：开启 **分页机制 (Paging)**，实现恒等映射与缺页异常 (#PF) 处理。
- **用户虚拟内存区域 (VMA)**：用户地址空间用 VMA 列表描述，缺页时先基于 VMA 判断访问是否合法，再决定是否按需分配映射。
- **内核堆分配器 (KHeap)**：基于链表的动态内存分配 (`kmalloc`/`kfree`)，支持自动堆扩展与内存块合并。
- **文件系统**：支持 ramfs、procfs、sysfs、devtmpfs、MinixFS（支持读写）
- **设备驱动模型**：支持 PCI/PCIe/NVMe 枚举，支持 NVMe 设备检测
- **系统调用**：支持文件操作、进程管理、内存管理等基础系统调用
- **测试框架**：`smoke` 测试程序，集成了多种测试用例
- 内核态极简 Shell（支持 `help`, `clear`, `echo`, `info`, `uptime`, `meminfo`, `alloc`, `vmmtest`, `heaptest` 等基础命令）

## 14. 驱动模型与模块初始化 (Driver Core & Initcalls)

### 设计背景
随着系统支持的驱动越来越多（如键盘、串口、VGA、TTY 等），如果将所有的初始化函数都硬编码在 `start_kernel`（或 `main.c`）中，会导致内核启动代码变得极其臃肿，且强耦合严重。特别是像硬件中断相关的驱动，如果不小心在中断描述符表 (IDT) 或内存系统初始化之前就启用，会导致内核瞬间崩溃（Triple Fault）。

为了实现优雅的架构，Lite OS 引入了类似 Linux 2.6 的 `initcall` 机制和“早期控制台分离”思想。

### 核心机制与入口
- **`__define_initcall` 宏**：在 `include/linux/init.h` 中，我们利用 GCC 的 `__attribute__((__section__(".initcall.init")))`，将所有驱动的初始化函数指针（例如被 `module_init(keyboard_driver_init)` 标记的函数）集中编译到同一个 ELF 段中。
- **启动解耦 (Early Console vs. Full Driver)**：
  - 在 `start_kernel` 早期，我们只调用类似 `init_serial()` 这样的“早期控制台 (Early Console)”函数。它仅通过轮询（Polling）方式输出字符，**绝不开启任何硬件中断**，这确保了内核在极其脆弱的早期阶段也能使用 `printf` 安全地打印日志。
  - 在系统的基础架构（内存、IDT、VFS）就绪后，内核通过 `rest_init` 进入 0号进程（Idle）循环，并 fork 出 **PID=1 的内核 init 线程**。
- **延迟加载 (`do_initcalls`)**：
  PID=1 的线程在进入用户态 `/sbin/init` 之前，会统一调用 `do_initcalls()`。此时它会遍历 `.initcall.init` 段的所有函数指针，批量执行 `keyboard_driver_init`、`serial_driver_init` 等。在这个阶段注册 IRQ 和开启硬件中断是绝对安全的。

## 15. 虚拟文件系统 (VFS) 标准化

### 结构体对齐
早期的 `fs_node` 结构体过于简单，无法很好地映射真实的 UNIX 文件系统模型。我们将其重构为 `struct inode`，严格对齐 Linux 2.6 的关键字段：
- `i_ino`：唯一的 Inode 编号。
- `i_mode`：文件类型与权限掩码。
- `i_size`：文件大小。
- `i_fop` / `i_mapping`：分别指向文件操作表和内存页缓存（Page Cache）。
此外，我们还引入了 `struct dentry` 来表示目录项缓存树（dcache），并将 `struct file` 与 `dentry` 强绑定。根目录的挂载也进行了精简，`vfs_root_dentry` 会在第一次挂载时直接分配好。`vfs_mount` 负责将设备根节点挂载到指定路径，并为其分配 `struct vfsmount`。

### 伪文件系统动态 Inode 分配
- **设计原理**：对于基于内存的伪文件系统（如 `ramfs`、`procfs`），节点凭空产生。为了避免全局 Inode 号冲突，引入了 VFS 层的统一分配器。
- **入口**：`get_next_ino()` (位于 `fs/inode.c`)，提供全局单调递增的伪 Inode 号分配。

## 16. 用户态与内核态的彻底隔离 (Shell 剥离)

### 设计背景
操作系统的交互界面不应运行在 Ring 0。此前的内置内核 Shell 虽然方便调试，但打破了微内核/单核OS的职责边界。

### 重构细节
- **完全移除 `kernel/shell.c`**：内核不再负责解析用户的键盘输入和执行相关的终端命令。
- **用户态 `shell.elf`**：通过 1 号进程 `init.elf` 在用户态挂载文件系统后，`fork + exec` 启动真正的用户态 Shell。所有的交互均通过标准系统调用 (`SYS_READ`, `SYS_WRITE`, `SYS_GETDENTS`) 完成。
- **内核测试下放与 C 语言环境**：原内核 Shell 中的测试被移植到用户态。我们建立了基于 C 语言的用户态运行时（`ulib`），并通过 `/bin/smoke` 程序来验证内核功能的正确性（包括 `fork`, `waitpid`, 文件 IO 和缺页异常隔离等）。

## 17. 任务调度与系统调用 (Task & Syscalls)

### ZOMBIE 状态与 Wait 语义
- **ZOMBIE 状态**：当子进程调用 `SYS_EXIT` 或被异常（如 unhandled page fault）杀死时，它不会立刻被完全销毁，而是进入 `TASK_ZOMBIE` 状态，并保留其退出码和退出原因，等待父进程回收。
- **资源清理**：在变为 ZOMBIE 时，进程的页表（`mm`）、打开的文件描述符（`fd`）等大块资源会被提前释放（`do_exit_reason`），仅保留 `struct task_struct` 结构体本身以供父进程查询状态。
- **Wait 语义**：父进程通过 `SYS_WAITPID` (对应 `sys_waitpid`) 阻塞等待。若子进程尚未退出，父进程会通过 `wait_queue_block_locked` 进入 `TASK_BLOCKED` 状态，并主动放弃 CPU。

### 内核态主动放弃 CPU (Yield) 机制
- **调度陷阱**：在内核态的循环（如 `waitpid` 的轮询）中，如果任务不放弃 CPU，会导致死锁或极高的 CPU 占用。
- **安全让权**：
  - 若任务已处于 `TASK_BLOCKED`（等待某事件），只需通过 `sti; hlt` 安全地进入低功耗状态，等待下一次时钟或硬件中断唤醒。
  - 若任务处于 `TASK_RUNNABLE`，则需通过软中断（如 `int $0x80` 传递 `SYS_YIELD`）强制触发内核堆栈的上下文保存和调度，防止在原调用栈上无限递归。

## 18. 文件系统节点删除与内存回收 (Unlink & Page Cache)

### 设计背景
为了支持文件删除，不仅需要从虚拟文件系统 (VFS) 的目录项中将其移除，还需要彻底释放该文件占用的物理内存（Page Cache），防止内存泄漏。

### 核心实现
- **`SYS_UNLINK` 系统调用**：新增 `unlink` 接口，对应系统调用号 `12`。
- **`vfs_unlink`**：在 VFS 层进行路径解析，切分出 `parent` 目录和 `basename`，获取父目录的 `inode` 并调用其底层文件系统的 `unlink` 方法。
- **`ramfs_unlink` 与 内存回收**：
  - 通过 `finddir` 找到目标子节点。
  - 调用 `truncate_inode_pages(inode->i_mapping, 0)` 遍历并释放该文件分配的所有物理页面（`free_page`）。
  - 将节点的 `flags` 置 0，并在 `generic_readdir` 和 `generic_finddir` 中主动忽略被标记为删除的节点，实现软删除。
- **用户态集成**：在 `ulib` 中封装 `unlink`，并在 Shell 中新增了 `rm` 命令。`smoke` 在测试文件 I/O 完毕后也会自动调用 `unlink` 清理测试文件。
