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

### `gdt.h` & `gdt.c`
GDT 用于定义内存的分段机制。虽然在平坦模式下，所有段都指向 0 到 4GB 的全量空间，但 x86 架构仍强制要求 GDT 存在以定义代码执行权限（Ring 0 / Ring 3）。

*   **`struct gdt_entry_struct`**：GDT 表项结构，包含段基址（32位）、段界限（20位）、访问权限和粒度标志。
*   **`struct gdt_ptr_struct`**：用于传递给 `lgdt` 指令的指针结构，包含 GDT 的界限（大小）和基址。
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

### `idt.h` & `idt.c`
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

### `isr.h` & `isr.c`
这是中断处理的高级 C 语言层。

*   **`struct registers`**：定义了当中断发生时，CPU 和汇编桩压入栈的寄存器结构，便于 C 函数读取现场状态。
*   **`pic_remap(void)`**
    *   可编程中断控制器 (PIC) 初始化。
    *   **关键作用**：BIOS 默认将 IRQ 0-7 映射到中断 8-15，但这与 x86 CPU 保留的异常（如 Double Fault）冲突。此函数向 PIC 发送 ICW 命令，将主片 IRQ 映射到 32-39，从片映射到 40-47。
    *   `outb(0x21, 0xFC)`：修改主片掩码，**仅开启 IRQ0（时钟）和 IRQ1（键盘）**。
*   **`isr_install(void)`**
    *   调用 `idt_set_gate` 将 0 到 31 号异常的汇编处理入口（`isr0` - `isr31`）注册到 IDT。
*   **`irq_install(void)`**
    *   调用 `pic_remap` 初始化硬件中断。
    *   将 IRQ0 和 IRQ1 的汇编处理入口注册到 IDT 的 32 和 33 号向量。
*   **`isr_handler(registers_t *regs)`**
    *   **CPU 异常总入口**。如果发生严重错误（如除零、缺页），会打印红色的 `KERNEL PANIC!` 以及具体的异常名称（通过 `exception_messages` 数组查询），然后进入死循环（Halt）。
*   **`irq_handler(registers_t *regs)`**
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
    *   **传递参数**：`push %esp`，将当前栈指针（即 `registers_t` 结构体的起始地址）作为参数传给 C 函数。
    *   **调用 C 函数**：`call isr_handler` 或 `call irq_handler`。
    *   **恢复现场**：恢复数据段寄存器，`popa` 恢复通用寄存器。
    *   **返回**：清理错误码和中断号，执行 `iret` 从中断返回。

---

## 5. 键盘驱动

### `keyboard.c`
基于 PS/2 控制器的基础键盘驱动。

*   **`kbdus` 数组**：一个包含 256 个元素的查找表，将键盘硬件发出的扫描码 (Scancode) 映射为对应的 ASCII 字符。
*   **`keyboard_callback(registers_t *regs)`**
    *   中断回调函数。当用户按键时被 `irq_handler` 触发。
    *   `inb(0x60)`：从键盘数据端口读取扫描码。
    *   **断码判断**：如果扫描码最高位为 1 (`scancode & 0x80`)，表示按键释放，当前逻辑选择忽略。
    *   **通码处理**：对于按键按下，通过 `kbdus` 数组查找字符并调用 `terminal_putchar` 显示在屏幕上。
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
    *   `terminal_initialize`：清空屏幕。
    *   `terminal_scroll`：**滚屏功能**。当输出到达屏幕底部（第 25 行）时，将第 1-24 行的数据拷贝到 0-23 行，并清空最后一行，防止内容被覆盖。
    *   `terminal_putchar`：处理字符输出。新增对退格键 `\b` 的支持（光标左移并写入空格覆盖），直接向物理内存地址 `0xB8000`（VGA 显存）写入带有颜色属性的字符数据。
    *   `strcmp` / `strncmp`：由于缺乏 C 标准库 (libc)，内核需自己实现基础的字符串比较函数，为 Shell 的命令解析提供支持（已重构至 `libc.c`）。

---

## 7. 基础模块：定时器与 C 库

### `timer.h` & `timer.c`
可编程间隔定时器（PIT, Programmable Interval Timer）是操作系统感知时间、进行进程调度的基础。
*   **硬件原理**：PIT 有一个固定频率的内部时钟（1193180 Hz）。通过向其端口发送除数（Divisor），可以控制其产生 IRQ0 中断的频率。
*   **`init_timer(uint32_t frequency)`**
    *   注册 `timer_callback` 到 IRQ0。
    *   计算除数：`1193180 / frequency`（本内核设为 100Hz，即每 10 毫秒触发一次中断）。
    *   向 PIT 命令端口 `0x43` 写入 `0x36`（通道 0，先低后高，Rate Generator 模式）。
    *   将除数拆分为高低两字节，发送到 PIT 端口 `0x40`。
*   **时间追踪**：每次中断发生时，全局变量 `tick` 加 1。通过 `tick / frequency` 即可计算出系统运行的秒数 (`uptime`)。

### `libc.h` & `libc.c`
由于裸机环境下没有标准的 `<stdio.h>` 或 `<string.h>`，内核必须自带一套极简的 C 标准库。
*   **内存操作**：`memset`, `memcpy`，用于后续内存分配器的初始化与拷贝。
*   **字符串操作**：`strlen`, `strcmp`, `strncmp`。
*   **格式化输出**：
    *   `itoa`：核心转换函数，利用取模运算将整数（十进制或十六进制）转换为 ASCII 字符串。
    *   `printf`：基于可变参数宏（`stdarg.h`）实现的极简版打印函数，目前支持 `%d`（整数）、`%x`（十六进制）、`%s`（字符串）、`%c`（字符），极大方便了后续内存地址的调试与输出。

---

## 8. 物理内存管理 (PMM)

### `multiboot.h` & `pmm.c`
这是操作系统接管物理硬件资源的第一步。
*   **Multiboot 协议**：GRUB 在加载内核时，会把一张“系统地图”放在内存里，并通过寄存器 `%ebx` 告诉内核这张地图在哪。
*   **`kernel_entry` (中间层)**：我们在 `boot.s` 和 `kernel_main` 之间加了一个跳板，负责把汇编里的寄存器参数转换成 C 语言的函数参数。
*   **`pmm_init`**：读取 Multiboot 结构体，获取 `mem_lower` (低端内存) 和 `mem_upper` (高端内存) 的大小。
    *   **详细流程**：
        1.  统计总内存容量，并换算为物理页数量（每页 4KB）。
        2.  选择位图 (bitmap) 的放置位置：默认放在内核镜像末尾（`end` 符号），并额外避开 Multiboot 模块结构体与模块数据，防止覆盖 InitRD。
        3.  对齐位图起始地址到页边界，计算位图大小（`total_pages / 32`，不足则向上取整）。
        4.  初始化位图为全 1（全部视为“已占用”），再根据 BIOS E820 内存地图逐步释放可用区域。
        5.  释放规则：低端 1MB 保留不释放；仅释放位于 bitmap 之后的可用页，避免位图自身被分配导致自毁。
*   **`pmm_print_memory_map`**：遍历 BIOS 提供的 E820 内存地图。这张表详细列出了哪些物理地址是 `AVAILABLE` (可用 RAM)，哪些是 `RESERVED` (被硬件占用)。这是后续编写“物理页分配器”的唯一依据。
*   **位图分配器 (Bitmap Allocator)**：
    *   **原理**：将物理内存切分为 4KB 的页帧。用 1 个 bit 表示 1 个页的状态（0=空闲，1=占用）。
    *   **初始化**：计算所需位图大小，将其放置在内核代码段结束之后（通过链接脚本中的 `end` 符号定位），并初始化为全 1（默认占用）。然后根据内存地图将 `AVAILABLE` 的区域标记为 0（空闲）。
    *   **API**：
        *   `pmm_alloc_page()`：线性扫描位图，找到第一个为 0 的位，置 1 并返回物理地址。
        *   `pmm_free_page(void* p)`：根据物理地址计算位图索引，将对应位置 0。
*   **当前内存布局（可扩展）**：
    *   **低端内存 (0 ~ 1MB)**：BIOS 数据区、IVT、VGA 显存、Multiboot 结构体等，保持保留不分配。
    *   **内核镜像**：从 `0x100000` 开始，依次是 `.text/.rodata/.data/.bss/.bootstrap_stack`，由链接脚本布局。
    *   **模块区 (可变)**：Multiboot 模块结构体数组 + 模块数据（InitRD 以及后续新增模块），大小随模块数量变化。
    *   **PMM 位图**：放在“内核镜像 + 模块区”之后，并对齐到页边界。
    *   **可用物理页区**：位图之后的可用 RAM，由 E820 标记为 `AVAILABLE` 的区域逐页释放。
    *   **扩展性**：如果未来增加更多模块，它们会占用模块区并向上增长，PMM 位图总是移动到模块区之后，确保不会被覆盖。

---

## 9. 虚拟内存管理 (VMM) 与分页

### `vmm.h` & `vmm.c`
开启分页 (Paging) 是操作系统进入保护模式完全体的重要标志。它引入了虚拟地址到物理地址的映射机制。

*   **页目录 (PDE) 与页表 (PTE)**：
    *   采用 x86 标准的两级分页结构。每个表项 4 字节，包含物理基址和属性位（Present, Read/Write, User 等）。
*   **恒等映射 (Identity Mapping)**：
    *   在开启分页的瞬间，如果 CPU 的指令指针 (EIP) 指向的虚拟地址没有被映射，系统会立刻崩溃（Triple Fault）。
    *   因此，我们首先将物理地址 `0x000000 - 0x400000` (前 4MB) 映射到虚拟地址的 `0x000000 - 0x400000`。
*   **`vmm_init`**：
    1.  申请页目录（4KB 对齐）并清零，确保所有 PDE 初始为 not-present。
    2.  申请第一个页表，用于映射低端内存（至少覆盖 0~4MB）。
    3.  扩展恒等映射到 0~128MB：共 32 个页表，每张表映射 4MB，确保内核、InitRD 和低端内存都可访问。
    4.  将每张页表挂入页目录（PDE 标记为 Present/RW），并填充 1024 个 PTE（每页 4KB）。
    5.  注册缺页异常 (#PF, Interrupt 14) 处理函数，避免开启分页后缺页直接 Triple Fault。
    6.  加载 `CR3` 指向页目录地址，再置位 `CR0.PG` 开启分页。
    7.  开启后所有地址走页表翻译，恒等映射保证当前 EIP 仍可取指执行。
*   **`vmm_map_page`**：
    *   作用：建立“物理页 → 虚拟页”的映射，必要时自动分配新的页表并写入页目录。
    *   入参来源：
        *   `phys_addr` 通常来自 `pmm_alloc_page()` 或 Bootloader 提供的模块物理地址（如 InitRD）。
        *   `virt_addr` 由内核虚拟地址布局策略决定（当前多为恒等映射，后续可扩展高地址映射）。
    *   关键步骤：
        *   计算 PDE/PTE 下标定位页表项。
        *   若对应 PDE 不存在，分配新页表并刷新 CR3 以清空 TLB。
        *   写入 PTE 后用 `invlpg` 刷新该页的 TLB。
*   **VMM 查询接口**：
    *   `vmm_is_mapped`：判断某个虚拟地址是否已建立 PTE 映射。
    *   `vmm_virt_to_phys`：将已映射的虚拟地址转换为物理地址（包含页内偏移）。
*   **`page_fault_handler`**：
    *   当访问非法内存地址时触发（#PF，Interrupt 14）。
    *   从 `CR2` 寄存器读取导致错误的线性地址（faulting address）。
    *   解析错误码（Error Code），区分：
        *   是否存在 (Present/Not Present)
        *   读/写访问
        *   用户态/内核态
        *   保留位错误或取指错误
    *   当前实现支持最小按需映射：
        *   对 not-present 且非 reserved 的缺页，分配物理页并映射到 fault 地址所在页，清零后返回继续执行。
        *   对低地址（< 0x1000）与 reserved/权限类错误仍直接 panic。
*   **`isr_handler` / `irq_handler` 调用流程**：
    *   **异常路径（ISR）**：
        *   `isrX` 汇编入口压入错误码/中断号，跳转到 `isr_common_stub`。
        *   `isr_common_stub` 保存现场、切换段寄存器，把 `registers_t*` 传给 `isr_handler`。
        *   `isr_handler` 若找到注册回调则调用，否则打印异常并 panic。
    *   **硬件中断路径（IRQ）**：
        *   `irqX` 汇编入口压入伪错误码/中断号，跳转到 `irq_common_stub`。
        *   `irq_common_stub` 保存现场并调用 `irq_handler`。
        *   `irq_handler` 先发 EOI，再调用对应回调或专用处理（如串口 IRQ4）。

---

## 10. 文件系统与 InitRD

### `initrd.h` & `initrd.c`
为了让内核不再是“光杆司令”，我们需要加载外部文件。InitRD (Initial Ramdisk) 是最简单的实现方式：引导加载器（如 QEMU/GRUB）把一个文件镜像加载到内存里，内核直接从内存读。

*   **Multiboot 模块**：
    *   GRUB 通过 `multiboot_info_t` 结构体中的 `mods_addr` 和 `mods_count` 字段告诉内核 InitRD 在内存的哪个位置。
    *   **关键坑点**：`mods_addr` 指向的是 `multiboot_module_t` 结构体数组，而不是文件数据本身！必须通过 `mod->mod_start` 获取文件内容的物理地址。
*   **`kernel_main` 中的 InitRD 初始化流程**：
    *   检查 `mods_count` 是否大于 0，确认引导器确实加载了模块。
    *   读取 `mod_start/mod_end` 得到 InitRD 物理地址区间，先做非法值检查。
    *   将 InitRD 覆盖的物理页做恒等映射（必要时补映射 128MB 以外的区域）。
    *   调用 `init_initrd(initrd_location)` 解析文件系统，构建 `fs_root`。
    *   根据返回值输出 “Ramdisk loaded/Failed” 日志。
*   **内存保护**：
    *   InitRD 占用的物理内存必须在 PMM 初始化时被标记为“已占用”，否则 `kheap` 或其他模块可能会申请到这块内存并覆盖文件数据。
*   **文件系统解析**：
    *   我们定义了一个极简的自定义文件系统格式（由 `mkinitrd.c` 生成）：
        *   前 4 字节：文件数量 (N)。
        *   接下来 N 个 Header：每个 Header 包含文件名、偏移量、长度。
        *   最后是文件数据。
    *   `init_initrd`：解析内存中的文件头，构建文件节点树 (`fs_node_t`)。
    *   `initrd_read`：根据文件节点的偏移量和长度，直接从内存中 `memcpy` 数据到用户缓冲区。
*   **`init_initrd` 关键流程**：
    *   做地址合法性检查，并通过 `vmm_is_mapped` 判断是否已映射。
    *   读取镜像头部的文件数量，并定位文件头数组起始位置。
    *   创建根目录节点并命名为 `initrd`（当前文件系统根还不是 `/` 语义）。
    *   为每个文件创建 `fs_node_t` 节点，设置 `read` 回调并将偏移转换为内存绝对地址。

---

## 11. 串口驱动与 Shell 交互

### `kernel.c` (Serial Part) & `shell.c`
为了支持在无图形界面（Headless）环境下调试和交互，我们实现了串口驱动。

*   **串口 (COM1) 初始化**：
    *   端口 `0x3F8`。
    *   设置波特率（Divisor）、数据位（8位）、停止位（1位）、校验位（无）。
    *   **中断使能**：开启“接收数据可用”中断 (Received Data Available)，这样当宿主机通过串口发送字符时，CPU 会收到 IRQ 4。
*   **中断处理 (IRQ 4)**：
    *   在 IDT 中注册中断号 36。
    *   在 PIC 中解除 IRQ 4 的屏蔽。
    *   中断发生时，读取端口 `0x3F8` 获取字符，并传递给 Shell。
*   **Shell 输入处理**：
    *   **回车键兼容**：不同终端可能发送 `\r` (CR) 或 `\n` (LF)。Shell 必须同时识别这两种字符作为命令结束符。
    *   **退格键支持**：识别 `\b` (0x08) 和 `DEL` (0x7F)，在缓冲区中删除字符并在屏幕/串口回显退格操作。

---

## 调试经验总结

在开发 InitRD 和 Shell 的过程中，我们遇到了两个典型问题，详细记录在 [docs/all_issues_summary.md](./docs/all_issues_summary.md) 中：
1.  **InitRD Triple Fault**：由于错误解引用 Multiboot 结构体指针，导致访问无效地址 `0xFFFFFFFF`，触发缺页异常并升级为 Triple Fault。解决方法是修正指针算术并确保 PMM/VMM 正确映射了 InitRD 内存区域。
2.  **Shell 无响应**：在 QEMU `-nographic` 模式下，输入来自串口而非键盘。必须实现串口中断处理 (IRQ 4) 才能接收输入。同时需要处理 `\r` 与 `\n` 的兼容性问题。

## 12. 内核堆分配器 (Kernel Heap)

### `kheap.h` & `kheap.c`
为了在内核中支持动态数据结构（如链表、树），我们需要实现 `kmalloc` 和 `kfree`。我们采用了一个简单的**链表式分配器 (Linked List Allocator)**。

*   **数据结构**：
    *   每个内存块由一个 `header_t` 头部管理，包含 `size` (大小)、`is_free` (是否空闲) 和 `next` (下一个块指针)。
    *   整个堆本质上是一个由 `header_t` 串联起来的单向链表。
*   **初始化**：
    *   堆的虚拟起始地址固定为 `0xC0000000` (3GB 处)。
    *   初始大小为 **1MB**（`KHEAP_INITIAL_SIZE = 0x100000`），虚拟范围为 `0xC0000000 ~ 0xC0100000`。
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

## 11. 交互式 Shell

### `shell.h` & `shell.c`
这是一个运行在内核态（Ring 0）的极简命令行解释器，它赋予了系统与用户交互的能力。
> **注意**：在标准的现代操作系统中，Shell（如 bash）是运行在用户态的普通应用程序，通过系统调用与内核通信。由于我们目前还没有实现分页、进程隔离和系统调用，因此暂时将这个极简 Shell 直接编译进内核。
*   **输入模型**：
    *   字符流统一进入 `shell_process_char`，逐字节处理回车、退格与普通字符。
    *   回车触发 `shell_execute`，将缓冲区封口后解析命令。
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
*   **任务状态查看**：
    *   `ps` 输出任务列表、状态与唤醒 tick。
*   **系统调用最小闭环**：
    *   `syscall` 使用 `int 0x80` 调用 `SYS_WRITE/SYS_YIELD` 进行测试。
*   **用户态入口测试**：
    *   `user` 启动最小 Ring 3 任务，执行 `int 0x80` 验证用户态→内核态调用。
*   **独立页表注意点**：
    *   用户态访问的页表项与页目录项必须同时设置 `PTE_USER`，否则会触发用户态缺页异常。

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
        *   `clear`：调用 `terminal_initialize` 清屏。
        *   `info`：打印内核的架构和特性信息。
        *   `echo`：使用 `strncmp` 匹配前缀，并打印参数部分。
    *   **重置缓冲区**：执行完毕后，将 `cmd_index` 归零，准备迎接下一条命令。
*   **`kernel_main(void)`**
    *   按顺序执行系统初始化：
        1.  `terminal_initialize()`：屏幕就绪。
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
- **物理内存管理 (PMM)**：解析 Multiboot 内存地图，使用**位图 (Bitmap)** 管理物理页分配与释放。
- **虚拟内存管理 (VMM)**：开启 **分页机制 (Paging)**，实现恒等映射与缺页异常 (#PF) 处理。
- **内核堆分配器 (KHeap)**：基于链表的动态内存分配 (`kmalloc`/`kfree`)，支持自动堆扩展与内存块合并。
- 内核态极简 Shell（支持 `help`, `clear`, `echo`, `info`, `uptime`, `meminfo`, `alloc`, `vmmtest`, `heaptest` 等基础命令）

## 推荐扩展路线（可逐步加入）
