# Lite OS → 32-bit Linux-like Kernel Roadmap (v2)

目标：把当前 Lite OS 演进为一个 **32 位、monolithic、Linux-like** 内核：用户态/内核态严格分离；核心子系统（trap/调度/MM/VFS）具备可扩展的内核语义；以 **procfs/sysfs** 为可观测与自描述的第一生产力；最终进入块设备与真实存储栈。

本版本定位：在 v1 的总体方向不变基础上，结合当前代码演进结果，明确“已完成/偏离/欠账”，并给出一份更可执行、可验收、可收敛的后续规划，避免接口与语义长期分叉。

## 0. 当前实现复盘（v2 基线）

已具备（按子系统归纳）：

- **启动与硬件基础**：Multiboot、VGA/串口、GDT/IDT/PIC/PIT、键盘 IRQ。
- **Trap/系统调用地基**：
  - `int 0x80` 使用 DPL=3 的 trap gate；syscall 入口不隐式 `cli`（保持可抢占语义）。
  - 用户指针校验与 `copyin/copyout`（uaccess）已形成最小闭环。
- **任务与阻塞语义**：
  - task 状态机 `RUNNABLE/SLEEPING/BLOCKED/ZOMBIE`、`wait_queue`、`task_wait` 回收闭环。
  - context switch 更新 `tss.esp0`（每任务内核栈顶），避免 Ring3→Ring0 共栈破坏。
- **内存管理（PMM/VMM/VMA）**：
  - PMM bitmap 分配器、VMM 分页与 0~128MB 恒等映射、per-task page directory。
  - 用户态 `#PF`：基于 VMA 校验权限（r/w/x）并按需分配；段权限支持只读页。
  - `brk` 雏形：维护 heap VMA 并依赖缺页按需分配物理页。
- **文件与命名空间雏形**：
  - initrd 只读文件系统（`struct vfs_inode` 模型）。
  - root 下挂载 `proc/`（tasks/sched/irq/maps）与 `dev/console`（最小设备节点）。
  - fdtable 已为 per-task（但 VFS 语义仍未收敛为统一 open/read/write/close）。
- **可观测性**：
  - `/proc/tasks`、`/proc/sched`、`/proc/irq`、`/proc/maps`（当前 maps 为“当前任务视角”的 VMA 列表）。

当前发现的两类“结构性偏差/欠账”（会阻塞后续阶段验收）：

- **MM 回收路径未与 VMA 语义对齐**：
  - 缺页/权限已经由 VMA 驱动，但退出回收仍主要依赖 `user_base/user_pages/user_stack_base` 的旧模型，无法覆盖 heap/brk 扩展映射的页，存在资源泄漏风险。
- **syscall/VFS 语义出现多轨并行**：
  - 已修正：核心 I/O 已收敛到 `SYS_OPEN/SYS_READ/SYS_WRITE/SYS_CLOSE` 的 fd 风格语义。
  - 后续仍需在 VFS 对象模型层面完成收敛（file/inode 等），避免 syscall 层长期特判。

## 1. 总体原则（v2 补充约束）

除 v1 原则外，v2 增加两条约束用于防止跑偏：

- **单一语义源**：运行时的合法性判断、权限、按需分配、回收必须由同一套对象（mm/vma）驱动，禁止“缺页用 VMA、回收用旧范围”。
- **ABI 收敛优先**：syscall 与 VFS 的核心 I/O 语义必须尽早收敛为一套（open/read/write/close + fdtable + file offset），禁止长期保留多套并行接口。

## 2. Phase 状态盘点（v2）

- **Phase A（Trap/内核栈/可抢占语义）**
  - A1 syscall trap gate + 不隐式 `cli`：已对齐。
  - A2 context switch 更新 `tss.esp0`：已对齐（实现层面仍是单例 TSS，但 esp0 已 per-task 更新）。
  - A3 抢占调度骨架：已具备时间片与切换计数（需持续用 `/proc/sched` 验证）。
- **Phase B（阻塞与等待）**
  - waitqueue 与阻塞读：已具备（shell 输入阻塞、`task_wait` 阻塞）。
  - runqueue/调度可扩展：当前为环形链表选择 runnable，后续可再对象化。
- **Phase C（MM 完善）**
  - C1 VMA 驱动缺页与权限：已具备最小版本（含只读段）。
  - C2 brk：已具备雏形（heap VMA + #PF demand alloc）。
  - C3 回收一致性：未完成（需优先修正）。
- **Phase D（VFS 对象化 + fdtable）**
  - D2 per-task fdtable：已落地。
  - D1 VFS 核心对象（inode/dentry/file/sb/mount）：未落地。
  - D3 procfs：已具备最小闭环（但 `/proc/<pid>/*` 语义未对齐）。
  - D2 里的 console `/dev/console`：已具备节点，但 read/write 语义仍存在 syscall 特判与多接口并存问题。

## 3. 纠偏优先级（Phase 0：止血与收敛）

这一阶段只做“必须先做对”的两件事，完成后再继续推进 C/D。

### P0.1 MM 回收：切换为 VMA 驱动（必须优先）

目标：
- 用户进程退出时，回收逻辑以 mm/vma 为唯一语义源，覆盖 text/rodata/data/bss/stack/heap/brk 扩展等所有用户映射页，并正确释放页表页。

状态：
- 已落地（回收改为遍历 VMA 释放用户页，并释放非内核共享页表页与页目录页）。

验收标准：
- 连续多次运行用户程序（包含 heap/brk 扩展与缺页）退出后，PMM 可用页数不持续下降（无明显泄漏）。
- 不出现 double-free 与页表页遗漏释放。
- `/proc/maps` 中的 VMA 与缺页/权限行为一致，且回收覆盖该范围。

### P0.2 syscall/VFS 语义收敛：统一到 read/write(fd)

目标：
- 核心 I/O syscall 收敛为 `open/read/write/close`（fdtable + file offset + ops）。
- fd=0/1/2 通过 task 创建时预置打开的 `/dev/console` 提供，而不是在 syscall 层永久特判。
  - 逐步淘汰过渡接口（例如 `SYS_FREAD`、`SYS_READFD/SYS_WRITEFD` 等并行形态），避免 ABI 分叉。

状态：
- 已落地（用户态与内核态测试路径已切换到 fd 风格 `SYS_READ/SYS_WRITE`，移除并行 I/O syscall）。

验收标准：
- 用户态 `cat.elf` 通过统一 read/write(fd) 路径跑通（不依赖专用 `FREAD`）。
- smoke 测试用例仍稳定通过，且 syscall ABI 不再出现“多套并行核心 I/O”。

## 4. Phase C（v2 继续推进：mm_struct 成形）

在 P0.1 完成后再继续：

- **C1.1 引入 mm_struct（task 内只保留 `mm*`）**
  - mm 持有：page directory、vma list、heap_base/brk、stack、统计信息等。
  - kernel thread 可无 mm；user process 必须绑定 mm。
- **C1.2 /proc/<pid>/maps 对齐**
  - 从“当前任务 maps”升级为 `/proc/<pid>/maps`，输出来自 mm。
- **C3.1 回收单路径**
  - mm teardown 单路径：释放用户页、释放页表页、释放 VMA；边界检查（溢出/跨 3G/guard）明确。

## 5. Phase D（v2 继续推进：VFS 对象化收敛）

在 P0.2 完成（I/O ABI 收敛）后再继续：

- **D1.1 最小 file 对象**
  - fdtable 存 `file*`（含 offset），而不是 `struct vfs_inode* + offset` 的“半 file”。
  - initrd/procfs/devfs 都通过统一 `file_ops` 暴露 read/write/open/close。
- **D1.2 最小路径解析与命名空间**
  - 逐步从“union root”过渡到 mount 模型（先能表达 `/proc`、`/dev`、`/` 三者关系即可）。
- **D2.1 console 作为普通文件**
  - `/dev/console` 的 read/write 通过 file_ops 实现，stdin/stdout/stderr 仅是默认 fd 绑定。

验收标准：
- procfs/sysfs/initrd/dev 都挂在统一命名空间；核心状态主要通过 `/proc`/`/sys` 观测。
- open/read/write/close 的语义稳定，fdtable 与进程绑定，且不再在 syscall 层做长期特判。

## 6. Phase E：存储栈主线（Block Layer → Cache → 磁盘 FS）

保持 v1 方向不变，但明确前置条件：
- 只有在 syscall/VFS/MM 三者语义收敛后（P0 完成 + D1 初步完成），才进入块设备与缓存层，避免把错误 ABI 带入存储栈导致大规模返工。
