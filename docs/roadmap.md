# Lite OS → 32-bit Linux-like Kernel Roadmap (Final)

目标：把当前 Lite OS 演进为一个 **32 位、monolithic、Linux-like** 内核：用户态/内核态严格分离；核心子系统（trap/调度/MM/VFS）具备可扩展的内核语义；以 **procfs/sysfs** 为可观测与自描述的第一生产力；最终进入块设备与真实存储栈。

本版本融合了两类思路：

- **Linux-like 的地基优先**：trap 语义、每线程内核栈、抢占调度先做对，否则后续子系统在错误地基上越做越难。
- **闭环与可观测性并行**：每推进一个子系统，同时提供 procfs/sysfs 可观测入口，让“可验证”成为常态。

## 0. 当前实现复盘（作为 Roadmap 的输入）

已具备：

- Multiboot 启动、GDT/IDT/PIC/PIT、键盘与串口中断路径。
- 分页、用户态指针校验与 `copyin/copyout`。
- 用户态 ELF 加载与 Ring3 进入路径。
- task 运行、sleep/yield、ZOMBIE + wait 的回收闭环。
- InitRD 只读文件系统雏形与最小文件 syscall（用于验证边界）。

关键缺口（会结构性阻塞 Linux-like 目标）：

- **syscall/trap 语义不对齐**：syscall 入口不应隐式 `cli`，需要可抢占语义。
- **TSS.esp0 不是 per-task**：用户态陷入内核需要每线程独立 kernel stack，并在 context switch 更新 `esp0`。
- **MM 缺少 mm/vma 语义**：当前缺页与权限判断偏硬编码，无法支撑 mmap/heap/权限演进。
- **VFS 对象模型缺失**：缺 inode/dentry/file/superblock/mount 与 per-process fdtable。
- **阻塞/唤醒机制不足**：busy-yield 在 IO 与等待场景会快速失控，需要 waitqueue 与统一阻塞点。

## 1. 总体原则（必须长期坚持）

- **边界优先**：所有 Ring3/Ring0 交互必须走 uaccess（校验 + copyin/copyout），杜绝内核直接解引用用户指针。
- **地基正确性优先**：trap/内核栈/抢占属于地基，必须先稳住再谈复杂功能。
- **对象化设计**：逐步形成稳定对象模型：task/process、mm/vma、file/inode/dentry/sb/mount、device/block。
- **可观测性前置**：每一阶段都有可读的 `/proc` 或 `/sys` 输出；否则调试成本会指数上升。
- **先语义后性能**：先保证隔离、回收、错误处理与接口稳定，再做缓存与优化。

## 2. Phase A：Trap/中断语义 + per-task 内核栈（用户态/内核态分离的地基）

目标：让 Ring3→Ring0 的路径在语义上对齐 Linux-like：syscall 不隐式关中断；可抢占；每线程独立 kernel stack；异常/中断/系统调用统一可靠。

- **A1. syscall 入口语义修正**
  - 为 syscall（vector 0x80）提供独立汇编入口，避免通用 ISR 宏里的 `cli`。
  - 在 IDT 中将 syscall 设置为 trap gate（DPL=3），确保 IF 语义可控且可抢占。
- **A2. per-task kernel stack 与 TSS.esp0 更新**
  - 每个用户 task 拥有独立内核栈；context switch 时更新 `tss.esp0` 指向当前 task 的 kernel stack top。
  - 明确“内核栈”与“用户栈”的切换规则（硬件通过 TSS 完成 Ring3→Ring0 的栈切换）。
- **A3. 抢占调度骨架**
  - IRQ0 驱动时间片与 `need_resched`，使 syscall/中断返回路径支持 reschedule。
  - 明确 preempt 禁止区的最小策略（后续可扩展）。

验收标准：
- syscall 执行期间 IF 语义正确，时钟中断可抢占 syscall。
- 多个用户任务并发陷入内核不共享内核栈，不出现栈破坏类随机崩溃。
- `/proc/irq` 或 `/proc/sched`（简化版）能观测到 tick、切换次数等关键指标。

## 3. Phase B：调度与阻塞语义完善（从“能跑”到“可扩展”）

目标：形成 Linux-like 的调度与阻塞语义：状态机清晰、阻塞点统一、waitqueue 贯穿 IO 与同步。

- **B1. task state 与 runqueue**
  - 明确：RUNNABLE/SLEEPING/BLOCKED/ZOMBIE 等状态与转换路径。
  - 引入 runqueue（单队列即可），确保选择下一个 runnable 的逻辑可扩展。
- **B2. waitqueue/阻塞原语**
  - `wait(pid)`、`read(fd)`、sleep 等统一走阻塞唤醒，不再 busy-yield。
  - 引入基础同步原语（spinlock/irqsave 级别即可）。
- **B3. 进程模型骨架**
  - 区分 kernel thread 与 user process（是否绑定 mm）。
  - 以 `exec` 为中心对齐：加载用户程序必须与 mm/vma 结构对齐（fork/clone 后续再做）。

验收标准：
- 系统内不存在依赖循环 yield 的等待逻辑；都能阻塞并被事件唤醒。
- `/proc/tasks` 能看到状态、阻塞原因（简化字段也可）、上下文切换统计。

## 4. Phase C：MM 完善（mm_struct + vma 驱动缺页与权限）

目标：把内存管理从“特例逻辑”升级为“统一语义”：VMA 描述用户空间；缺页、权限、按需分配完全由 VMA 决定。

- **C1. mm_struct + vma**
  - VMA 描述：text/rodata/data/bss、stack、heap、mmap 区。
  - `#PF`：查 VMA → 校验权限（读/写/取指）→ demand-zero/demand-alloc → 设置 PTE 权限。
- **C2. 用户堆接口**
  - 先实现 `brk`（更 Linux）或最小 `mmap(MAP_ANON)`（更通用）之一。
- **C3. 回收与一致性**
  - 进程退出时 mm 的回收路径清晰可验证，避免 double-free、遗漏释放页表页等问题。

验收标准：
- `#PF` 行为可解释且一致：合法 VMA 内按需分配，非法访问必定 kill 并记录原因。
- `/proc/<pid>/maps` 能列 VMA，且与缺页/权限行为一致。

## 5. Phase D：VFS 对象化（procfs/sysfs 优先）与 fdtable 内核化

目标：让“文件”成为统一内核对象语义：路径、打开实例、权限、设备、procfs/sysfs 都走同一套 VFS。

- **D1. VFS 核心对象**
  - 引入：`inode`、`dentry`、`file`、`superblock`、`mount`。
  - 最小路径解析（先支持绝对路径与单层目录也可），但对象关系必须正确。
- **D2. per-process fdtable**
  - 从全局 fd 表迁移到每进程 fdtable。
  - console 纳入 `/dev`（例如 `/dev/console`），`read/write` 对齐为 `read(fd,...)`/`write(fd,...)`。
- **D3. procfs（可观测闭环）**
  - `/proc/meminfo`、`/proc/tasks`、`/proc/<pid>/stat`、`/proc/<pid>/maps`。
- **D4. sysfs（对象树）**
  - `/sys/kernel/*`（版本、参数）、`/sys/devices/*`（console、initrd、后续 block 设备）。

验收标准：
- procfs/sysfs/initrd 都挂载到统一命名空间；核心状态主要通过 `/proc`/`/sys` 观测。
- VFS 的 open/read/write/close 语义稳定，fdtable 与进程绑定。

## 6. Phase E：存储栈主线（Block Layer → Cache → 磁盘 FS）

目标：进入真实存储路径，形成可扩展的 Linux-like 存储栈。

- **E1. 块设备抽象**
  - `block_device` + 最小请求队列；先 ramdisk 或 virtio-blk（二选一）。
  - 错误处理与边界检查优先（越界、短读短写、重试策略）。
- **E2. 缓存层**
  - 最小 page cache/buffer cache，使 VFS read/write 与缓存对齐。
- **E3. 一个磁盘文件系统**
  - 选一个实现成本可控的 FS（ext2/minix/fat 任选），日志型 FS 不建议早期引入。

验收标准：
- syscall → VFS → cache → block queue → device 的完整 I/O 路径打通，且在 `/sys`/`/proc` 可观测。

## 7. 下一步（明确的第一行动项）

- **第一步只做两件事**：
  - 为 syscall（0x80）做独立入口与 trap gate 语义（不隐式 `cli`，可抢占）。
  - 将 TSS.esp0 改为 per-task 更新（每线程独立内核栈）。

这两件事完成后，再进入 Phase B/C/D 的推进，才能保证后续子系统不会在错误地基上反复返工。

