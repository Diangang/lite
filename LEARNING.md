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
    *   初始化 5 个段：Null 段（必须为 0）、内核代码段、内核数据段、用户代码段、用户数据段。
    *   所有有效段的基址均为 `0`，界限均为 `0xFFFFFFFF`（4GB）。
    *   内核段权限包含 `Ring 0`，用户段权限包含 `Ring 3`。
    *   最后调用 `gdt_flush`。

### `gdt_flush.s`
*   **`gdt_flush`**
    *   `lgdt (%eax)`：将新的 GDT 地址加载到 CPU 的 GDTR 寄存器。
    *   更新所有数据段寄存器 (`ds`, `es`, `fs`, `gs`, `ss`)，使其指向偏移量 `0x10`（即内核数据段，GDT 中的第 3 个条目）。
    *   `ljmp $0x08, $.flush`：使用长跳转指令强制刷新代码段寄存器 (`cs`)，使其指向偏移量 `0x08`（内核代码段）。

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

## 8. 交互式 Shell

### `shell.h` & `shell.c`
这是一个运行在内核态（Ring 0）的极简命令行解释器，它赋予了系统与用户交互的能力。
> **注意**：在标准的现代操作系统中，Shell（如 bash）是运行在用户态的普通应用程序，通过系统调用与内核通信。由于我们目前还没有实现分页、进程隔离和系统调用，因此暂时将这个极简 Shell 直接编译进内核。

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
- 内核态极简 Shell（支持 `help`, `clear`, `echo`, `info`, `uptime` 等基础命令）

## 推荐扩展路线（可逐步加入）
