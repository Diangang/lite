# Lite Kernel

Lite 是一款用于学习和演示操作系统底层原理的极简 32 位 x86 内核。

## 项目目标
- 提供一个干净、可读性强、易于实验的操作系统基础框架。
- 展示从引导（Bootloader）到内核主循环（Kernel Main）的完整接管过程。
- 演示如何配置 GDT、IDT、PIC 以及如何编写基础的硬件驱动（如 PS/2 键盘）。
- 作为操作系统爱好者的入门跳板，后续可继续扩展内存管理、进程调度等高级特性。

## 当前特性
- **Multiboot 兼容**：可以通过 GRUB 或 QEMU 直接加载启动。
- **GDT & IDT**：实现了自定义的全局描述符表和中断描述符表。
- **异常处理**：捕获 CPU 的 0-31 号异常，防止内核静默崩溃（提供 Kernel Panic 机制）。
- **硬件中断**：配置了 8259A PIC，正确映射了 IRQ，解决了与 CPU 异常的冲突。
- **基础驱动与模块**：
  - VGA 文本模式输出（支持退格擦除和自动滚屏）。
  - 串口（COM1）日志输出（方便在无图形界面下调试）。
  - PS/2 键盘驱动。
  - **PIT 可编程定时器**（提供系统 Tick 和 uptime 支持）。
  - **物理内存管理 (PMM)**：
    - Multiboot E820 内存地图解析。
    - **Bitmap 位图分配器**：支持按页 (4KB) 粒度的物理内存分配 (`alloc`) 与释放 (`free`)。
  - **虚拟内存管理 (VMM)**：
    - **分页机制 (Paging)**：开启 x86 保护模式分页，设置 CR3 和 CR0 寄存器。
    - **恒等映射**：将物理内存前 128MB 映射到相同的虚拟地址，保证内核与低端内存可访问。
    - **缺页异常处理**：捕获 `#PF` (Interrupt 14)，支持最小按需映射（not-present 缺页自动分配并映射）。
    - **映射查询**：提供虚拟地址是否已映射与虚实地址转换的辅助接口。
  - **内核堆分配器 (KHeap)**：
    - 实现了 `kmalloc` 和 `kfree`，支持动态内存分配。
    - 采用 **First-Fit** 策略与空闲块 **合并 (Coalescing)** 算法。
    - 初始化时会映射 1MB 堆空间并建立空闲链表，`kmalloc` 失败时会按需扩展堆空间。
  - **极简标准 C 库**（实现了 `printf`, `memset`, `memcpy`, `itoa` 等核心函数）。
  - **InitRD (Initial Ramdisk)**：
    - 支持通过 Multiboot 协议加载外部文件系统镜像。
    - 实现了简单的只读文件系统解析，支持读取文件内容。
- **交互式 Shell**：
  - 内置极简内核态 Shell，支持 `help`, `clear`, `info`, `echo`, `uptime`, `meminfo`, `alloc`, `vmmtest`, `heaptest`, `ls`, `cat`, `demo`, `yield`, `sleep`, `ps`, `syscall` 等命令（demo 默认关闭）。
  - **双模式输入输出**：同时支持 VGA 显示器+键盘 和 **串口 (COM1)** 终端交互。

## 构建与运行

### 依赖环境
- Linux 系统（推荐 Ubuntu/Debian）
- `gcc` 和 `binutils`（用于编译 32 位代码）
- `qemu-system-i386`（用于运行和调试）
- `xorriso` 和 `mtools`（仅当需要打包为 ISO 镜像时需要）

### 编译与启动
在终端中进入 `lite` 目录，执行以下命令：

```bash
# 清理并编译内核二进制 (myos.bin)
make clean && make

# 使用 QEMU 启动内核（图形界面模式）
# 注意：此模式下 Shell 输入依赖 QEMU 窗口焦点
qemu-system-i386 -kernel myos.bin -initrd initrd.img -m 512M

# [推荐] 使用调试脚本启动（无头模式 + 串口交互）
# 适合在无图形界面的服务器或 SSH 环境下开发
./debug.sh
```

*提示：使用 `./debug.sh` 启动后，您可以在当前终端直接与 OS Shell 交互。按 `Ctrl+A` 然后按 `X` 退出 QEMU。*

## 学习指南
如果您想深入了解本项目每一行代码的运作原理，请阅读 [LEARNING.md](./LEARNING.md)。该文档详细拆解了引导流程、中断上下文切换、驱动编写等核心细节。

此外，在 `docs/` 目录下还提供了以下参考资料：
- [all_issues_summary.md](./docs/all_issues_summary.md)：项目开发过程中的疑难 Bug 及排查过程汇总（包含 InitRD 和串口交互等经典问题）。
- [os_knowledge_qa.md](./docs/os_knowledge_qa.md)：操作系统底层核心概念（如 IDT、PIC、分页恒等映射等）的 Q&A 问答指南。
