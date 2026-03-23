# Lite OS 架构演进与学习指南

本文档记录了 Lite OS 向 Linux 2.6 架构演进过程中的核心设计原理、入口与关键数据结构。

## 1. 驱动模型与模块初始化 (Driver Core & Initcalls)

### 设计背景
早期的 Lite OS 将所有设备和驱动的注册硬编码在 `device_model_init` 中，导致内核启动流程臃肿且缺乏扩展性。为了解耦设备（硬件描述）与驱动（软件逻辑），我们引入了完全对齐 Linux 2.6 的 `module_init` 机制。

### 核心机制与入口
- **`.initcall.init` 段**：在 `arch/x86/kernel/linker.ld` 中定义了专属的初始化函数指针段。
- **`module_init(fn)` 宏**：通过 GCC 的 `__attribute__((__section__(".initcall.init")))`，将分散在各个驱动文件中的初始化函数指针自动收集到该段中。
- **入口**：在 `init/main.c` 中：
  1. 调用 `driver_init()`：仅初始化总线骨架（如 `platform_bus`）。
  2. 调用 `do_initcalls()`：遍历并执行 `.initcall.init` 段中的所有函数。
- **板级描述 (`setup.c`)**：架构相关的硬件注册剥离至 `arch/x86/kernel/setup.c`，在其中通过 `module_init` 向总线注册 `console` 和 `ramfs` 等静态设备。

## 2. 虚拟文件系统 (VFS) 标准化

### 结构体对齐
`struct inode` 的字段已完全对齐 Linux 标准命名：
- `i_ino`：节点号，替代原有的 `inode`。
- `i_mode`：权限与模式掩码，替代原有的 `mask`。
- `i_size`：文件大小，替代原有的 `length`。

### 伪文件系统动态 Inode 分配
- **设计原理**：对于基于内存的伪文件系统（如 `ramfs`、`procfs`），节点凭空产生。为了避免全局 Inode 号冲突，引入了 VFS 层的统一分配器。
- **入口**：`get_next_ino()` (位于 `fs/inode.c`)，提供全局单调递增的伪 Inode 号分配。

## 3. 用户态与内核态的彻底隔离 (Shell 剥离)

### 设计背景
操作系统的交互界面不应运行在 Ring 0。此前的内置内核 Shell 虽然方便调试，但打破了微内核/单核OS的职责边界。

### 重构细节
- **完全移除 `kernel/shell.c`**：内核不再负责解析用户的键盘输入和执行 `ls` / `cat` 等命令。
- **用户态 `ush.elf`**：通过 1 号进程 `init.elf` 在用户态挂载文件系统后，`fork + exec` 启动真正的用户态 Shell。所有的交互均通过标准系统调用 (`SYS_READ`, `SYS_WRITE`, `SYS_GETDENTS`) 完成。
- **内核测试下放**：原内核 Shell 中的破坏性测试（如触发 `#PF` 缺页异常）被移植到用户态独立程序 `/ktest.elf` 中，真实模拟用户态非法访问引发内核自我保护的场景。


## 4. 任务调度与系统调用 (Task & Syscalls)

### ZOMBIE 状态与 Wait 语义
- **ZOMBIE 状态**：当子进程调用 `SYS_EXIT` 或被异常（如 unhandled page fault）杀死时，它不会立刻被完全销毁，而是进入 `TASK_ZOMBIE` 状态，并保留其退出码和退出原因，等待父进程回收。
- **资源清理**：在变为 ZOMBIE 时，进程的页表（`mm`）、打开的文件描述符（`fd`）等大块资源会被提前释放（`task_exit_with_reason`），仅保留 `task_t` 结构体本身以供父进程查询状态。
- **Wait 语义**：父进程通过 `SYS_WAITPID` (对应 `task_wait`) 阻塞等待。若子进程尚未退出，父进程会通过 `wait_queue_block_locked` 进入 `TASK_BLOCKED` 状态，并主动放弃 CPU。

### 内核态主动放弃 CPU (Yield) 机制
- **调度陷阱**：在内核态的循环（如 `waitpid` 的轮询）中，如果任务不放弃 CPU，会导致死锁或极高的 CPU 占用。
- **安全让权**：
  - 若任务已处于 `TASK_BLOCKED`（等待某事件），只需通过 `sti; hlt` 安全地进入低功耗状态，等待下一次时钟或硬件中断唤醒。
  - 若任务处于 `TASK_RUNNABLE`，则需通过软中断（如 `int $0x80` 传递 `SYS_YIELD`）强制触发内核堆栈的上下文保存和调度，防止在原调用栈上无限递归。
