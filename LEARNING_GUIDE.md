# lite 内核学习指导

## 目标与范围
- 以最小可运行内核为基础，逐步扩展功能
- 保持工程结构简单，便于学习与演进

## 当前工程结构
- boot.s：使用 Multiboot 协议的启动入口，设置栈并跳转到内核
- kernel.c：最小内核逻辑（VGA 文本输出）
- linker.ld：链接脚本，定义内核从 1MB (0x100000) 开始布局各段
- Makefile：构建内核二进制 `myos.bin`，并支持打包为 GRUB 引导的 `myos.iso`

## 构建与运行
- 构建内核
  - `make clean && make`
- 运行（直接加载内核）
  - `qemu-system-i386 -kernel myos.bin -curses`
- 打包为 ISO 并运行（需要安装 xorriso 和 mtools）
  - `make iso`
  - `qemu-system-i386 -cdrom myos.iso -curses`

## 运行输出通道
- VGA 文本输出
  - 通过写显存 `0xB8000` 显示文本
  - 适用于 `-curses` 模式或图形窗口的 QEMU 启动方式

## 当前内存模型
- 未启用分页：虚拟地址 = 物理地址
- 内核加载起始地址：1MB (0x100000)，由 GRUB 等 Multiboot 加载器保证
- 栈：boot.s 中静态分配 16KiB
- 目前没有动态内存管理器

## 段布局（linker.ld）
- .multiboot：Multiboot 协议头部（必须放在最前面）
- .text：代码
- .rodata：只读常量
- .data：已初始化全局变量
- .bss：未初始化全局变量（运行时清零）

## 关键启动流程
1. BIOS 加载 Bootloader (如 QEMU 内置加载器或 GRUB)
2. Bootloader 解析内核的 Multiboot 头部
3. Bootloader 切换 CPU 到 32位保护模式，并加载内核到 1MB 处
4. 跳转到 boot.s 中的 `_start` 执行
5. 设置内核栈并调用 `kernel_main`
6. 初始化终端输出

## 当前最小功能清单
- Multiboot 协议兼容的内核入口
- 文本输出（VGA）
- 受控的内存段布局

## 推荐扩展路线（可逐步加入）
1. 内存探测（读取 BIOS E820 内存地图）
2. 最小物理内存分配器（bump 或 bitmap）
3. 定时器与中断（GDT/IDT + PIT）
4. 键盘输入（PS/2）
5. 简易驱动框架与系统调用雏形

## 维护约定
- 每次新增功能同步更新本指导文档
- 保持内容精炼、结构稳定、按模块扩展
