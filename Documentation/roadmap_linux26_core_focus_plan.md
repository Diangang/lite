# Lite OS → Linux 2.6（Core Focus Plan）

目的：把开发重心从“可观测/外设/展示”拉回到 Linux 2.6 的核心机制闭环，优先补齐 **进程生命周期 + MM（缺页/COW/回收）+ VFS 内存路径（page cache/writeback）**。sysfs/driver model/PCI/NVMe 仅作为验证载体，不作为主线目标。

## 0. 原则（本计划的约束）

1. **先闭环，再扩展**：每个阶段必须有可复现的触发方式 + smoke/自测覆盖 + 回归基线。
2. **先语义正确，再性能**：先保证 Linux 2.6 核心边界语义一致（返回码、状态、引用计数、生命周期），之后再优化。
3. **减少“横向”改动**：除非为核心闭环提供必要可观测性，否则不新增/扩展 sysfs/uevent/外设特性。
4. **以最小实现对齐 Linux 2.6 的关键路径命名**：函数命名与调用顺序尽可能与 2.6 可对照（便于学习与 diff）。

## 1. 当前核心完成度（按“能否闭环”评估）

### 已具备（可作为基线）
- 进程基本链路：fork/exec/exit/waitpid（smoke 覆盖）。
- 基本虚拟内存：mmap/munmap/mprotect/mremap（smoke 覆盖，但边界语义仍需加强）。
- 基本 VFS：ramfs + procfs + sysfs + devtmpfs（可启动用户态并运行 smoke）。

### 核心缺口（必须优先补齐）
- **进程生命周期边界**：僵尸/回收、reparent、wait 细节、信号与 wait 的交互（WNOHANG/WUNTRACED 等可后置，但状态编码需一致）。
- **缺页与匿名页/COW 的一致性**：写时复制正确性、匿名页与 VMA 保护、页引用计数与释放时机。
- **回收路径最小闭环**：在内存压力下能稳定回收（哪怕策略很粗糙），并能解释“为什么回收/为什么 OOM”。
- **page cache/writeback 最小闭环**：文件数据路径能走 page cache，脏页能在合适时机回写（先对 ramfs 做“伪 writeback”闭环，再对块设备做真实 writeback）。

## 2. 新开发计划（按里程碑拆分）

### M0：启动阶段与内存布局对齐（P0，必须先做）

目标：把启动阶段“识别内存 → 建立内核地址空间 → 初始化 bootmem/page_alloc”的核心语义尽量做得和 Linux 2.6 一样，避免目前的“写死 0-128MB 映射”与“隐式依赖低端分配”的不确定性。

交付项（按优先级）：
- e820 等价物（内存图）：
  - 建立内部 `e820_table` 风格的数据结构（addr/size/type）。
  - 以 multiboot memory map 作为数据源，转换成 e820 表（RAM/RESERVED 这两类先闭环，ACPI/NVS/UNUSABLE 可后置）。
  - 提供 `memblock`/bootmem 风格的“可用区间迭代器”，禁止从 RESERVED 分配。
- 线性映射（direct mapping）动态化：
  - `paging_init()` 不再固定映射 0-128MB，改为按 `lowmem_end` 映射（至少覆盖实际 RAM 的低端部分）。
  - 明确 `PAGE_OFFSET` 直映区的上界与不可用区（为后续 vmalloc/fixmap/highmem 留出布局空间）。
  - 强化 direct map 边界约束：核心路径禁止对非 lowmem 物理地址使用 `phys_to_virt`（应走 `ioremap/kmap`）。
- 启动参数与内存布局可观测：
  - 在启动日志或 `/proc/meminfo` 增加关键指标：e820 RAM/RESERVED、lowmem_end、direct map 覆盖范围、spanned/present/managed 的差异。
  - 增加 `/proc/iomem`（e820 等价物可视化），用于验证 RAM/RESERVED/Kernel/initramfs 区间。

验收（必须全部满足）：
- QEMU 不同内存大小（例如 128M/256M/512M）启动一致：direct mapping 覆盖随内存变化而变化。
- page allocator 在 >128MB 的物理页存在时也能稳定分配并访问（不再“靠运气”）。
- `bin/smoke` 回归通过（512M 下 Large MMAP Touch 通过）。

说明：这个里程碑并不是“外围可观测性”，而是保证 MM/缺页/回收的所有后续工作都有可靠的地址空间前提。

### M1：进程生命周期与 wait/signal 闭环（P1 的核心部分）

目标：把 `do_fork → wake_up_new_task → do_exit → release_task` 的主链路收敛到 Linux 2.6 的语义边界，保证 **不会泄漏 task/mm/file**，并且 wait 状态编码可预测。

交付项：
- 统一退出路径：`do_exit()`、`exit_notify()`、`release_task()` 的职责边界。
- wait 语义补齐：
  - 可稳定回收多个子进程（并发 fork/wait 压测）。
  - 状态编码一致（exit code vs signal reason）。
- 最小 signal 闭环：
  - 至少支持 SIGKILL/SIGTERM/SIGCHLD 的默认处理。
  - 信号到达后能打断 sleep（若已有 sleep/wakeup 机制）。

验收（必须全部满足）：
- 新增 smoke 用例：fork 炸裂（循环 fork/exit/wait），无死锁、无泄漏、状态正确。
- QEMU 可复现：多次运行结果一致。

完成进展：
- ✅ 退出路径职责边界：`do_exit → exit_notify → release_task` 已闭环。
- ✅ wait 回收与状态编码：fork blast + waitpid 编码回归通过。
- ✅ signal 基础语义：SIGCHLD/SIGTERM/SIGKILL/SIGINT/SIG0 回归通过，sleep 可被 SIGCHLD 打断。

### M2：缺页 + 匿名页 + COW 正确性（P2 的核心前半）

目标：把匿名页生命周期与 COW 从“能跑”推进到“语义正确”：写时复制只发生一次、引用计数正确、释放路径可预测。

交付项：
- 缺页分类明确：present/not-present、write fault、user/kernel fault、权限错误的返回码。
- 匿名页与 COW：
  - fork 后父子共享只读页；第一次写触发复制；复制后父/子互不影响。
  - 释放路径：最后一个引用释放物理页，避免 double-free / leak。
- rmap 最小化：
  - 至少能从 page 找到一个/多个映射（可先做 “单一映射” 简化，但接口命名对齐 rmap）。

验收：
- 新增 smoke 用例：fork 后父子分别写同一匿名页，读回验证隔离；重复多次不崩溃。
- 内存压力测试：触发回收/分配失败时行为可解释（不要求 swap 先完成）。

完成进展：
- ✅ 缺页分类统计：/proc/pfault 导出并回归验证。
- ✅ COW 语义与可观测：/proc/cow + Test 22–25 覆盖隔离、单次复制、释放路径。

### M3：回收最小闭环（P2 的核心后半）

目标：在内存压力下，系统能继续运行，并且“回收/退化/失败”的策略可控。

交付项（按优先级）：
- LRU/active/inactive 的最小框架（可以简化，但必须能让页面进入“可回收集合”）。
- vmscan 最小策略：
  - 优先回收 clean file pages（当 page cache 具备后）。
  - 其次回收匿名页（需要 swap 才能真正回收；在 swap 未完成前可限制匿名回收，避免破坏语义）。
- OOM 策略最小化：打印（或记录）关键指标并杀掉最合适的候选（单进程环境可先 kill 当前）。

验收：
- 新增压力测试程序（usr/）：malloc/触发缺页/触发回收，不死锁、不 silent corruption。

完成进展：
- ✅ vmscan 可观测：/proc/vmscan 导出 wakeups/tries/reclaims/anon/file。
- ✅ file cache 最小回收：page cache reclaim + vmscan 触发回收链路，Test 27/28 回归通过。

### M4：page cache + writeback 最小闭环（P3 的核心）

目标：把 VFS 的 read/write 路径真正走 page cache，并且有“脏页回写”的闭环。

交付项：
- address_space 与 page cache 的一致性：读路径命中缓存、写路径标脏。
- writeback 最小闭环：
  - 对 ramfs：可以是“回写到内存对象”的伪 writeback，但要走统一接口（对齐 Linux 的 writeback 结构）。
  - 对 block：等 M5 再做真实 IO。

验收：
- 新增 smoke 用例：同一文件多次写/读/覆盖，验证数据一致性与缓存命中（可用统计计数器验证）。

完成进展：
- ✅ page cache 脏页跟踪与 writeback 统计：/proc/writeback 导出 dirty/cleaned。
- ✅ writeback 触发闭环：可通过 /proc/writeback 写入触发刷回。
- ✅ 回归用例：Test 29 覆盖写→脏→刷回→统计收敛。
- ✅ page cache 命中/未命中统计：/proc/pagecache 导出 hits/misses，Test 30 覆盖写/读/回收路径。
- ✅ writeback 语义收敛：Test 31 覆盖覆盖写/部分写/截断与 discarded 统计。

### M5：块层主链路（P5 的入口，但只做最小闭环）

目标：打通 `bio → request → driver → completion`，并能挂载一个最小块 FS（只读也可）。

交付项：
- 最小块设备（优先 virtio-blk 或 ramdisk；NVMe 作为后续扩展，不作为第一目标）。
- buffer cache 与 page cache 协同（哪怕策略很简化）。
- 最小 FS：minix/ext2 的只读解析，或自定义只读 FS，目标是验证 block+cache 主链路正确。

验收：
- QEMU 中挂载块设备并读取文件；在压力下不崩溃。

## 3. 每个里程碑的“强制回归基线”

每次提交/合并前必须满足：
- `make -j$(nproc)` 通过
- `qemu-system-i386 -machine q35 ... -device nvme ...` 可启动并完成 `bin/smoke`（用于保证系统整体不退化）
- 新增的专项测试（M1~M5）必须可复现通过

## 4. 非目标（刻意延后）

- sysfs/uevent 的完整 Linux 语义（链接、属性、kobject 自动映射的完备性）
- 完整 O(1) 调度器与 SMP 负载均衡
- 网络栈、IPC、security
- 多架构与高端内存（highmem/PAE）

## 5. e820 是什么（以及 Lite 怎么做）

e820 是 Linux 2.6 在 x86 上用于描述“物理内存布局”的核心输入之一：它本质是一张“物理地址区间表”，每条记录包含 `(addr, size, type)`，告诉内核哪些物理区间是 RAM、哪些是保留区/ACPI/NVS/坏内存等。Linux 的启动内存管理（bootmem）依赖 e820 来决定：
- 哪些页能被分配
- direct mapping 覆盖到哪里（lowmem/highmem 边界）
- 哪些区间必须避开（BIOS/设备 MMIO/ACPI 等）

我们能实现吗：
- **能实现 e820 的“等价语义”**：在 QEMU+GRUB/multiboot 启动下，bootloader 会提供 multiboot memory map；我们可以把它转换成内部的 `e820_table`，在行为上等价于 Linux 的 e820 输入。
- **不必实现 BIOS int 0x15 的 real-mode e820 调用**：那是 BIOS/实模式接口，Linux 2.6 早期会走那条路；而我们的启动链路是 multiboot，直接吃 bootloader 给的 memory map 更合理，也更稳定。
