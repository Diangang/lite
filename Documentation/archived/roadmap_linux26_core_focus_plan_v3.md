# Lite OS → Linux 2.6（Core Focus Plan v3）

目的：在当前 M0–M5 已经“能闭环”的基础上，把实现从“最小可用”推进到“Linux 2.6 可对照的关键语义与结构”，重点落在 **块层/缓存/磁盘 FS（Minix）主链路**，并补齐 **设备驱动模型（PCI/PCIe/NVMe）** 以支持从 NVMe 块设备挂载 Minix。写回线程模型与 swap/anon reclaim 作为后置阶段。

## 1. 现状与 Linux 2.6 差异（只列主线）

### 1.1 已经对齐到“可闭环”的部分（现状）
- 启动期内存图与 lowmem：multiboot memory map → e820 等价物；direct map 按 lowmem_end 动态化；/proc/meminfo 与 /proc/iomem 可观测。
- MM 主链路：缺页、匿名页、COW、rmap 最小化；vmscan/file cache reclaim 最小闭环。
- VFS 内存路径：page cache + dirty/writeback 统计与触发闭环；blockdev 通过 page cache 实现读写与刷回。
- 最小块 FS：minixfs（最小子集）可挂载并读取文件（用于验证 block+cache 的读路径）。
- 设备驱动模型：PCI/PCIe/NVMe 枚举、namespace 暴露、/dev 节点与 sysfs 可观测。

### 1.2 与 Linux 2.6 的关键差异（优先级从高到低）
- 块层语义缺失：虽然有基本的 bio/request 结构，但缺少完整的 request_queue 与 I/O 调度器。
- buffer cache 实现不完整：缺少与 page cache 的深度集成，以及更完善的缓冲管理。
- 内存管理：缺少 radix-tree、更完整的 slab 实现，以及更精确的内存分配策略。
- 启动流程：与 Linux 2.6 的启动流程有差异，缺少 initcall 分级、param 解析等。
- 基础同步原语：缺少 spinlock、atomic 等核心同步机制。
- 时间管理：缺少完整的 timer 子系统和时间管理。
- 信号处理：信号处理机制不完整。
- 写回线程模型与 swap：暂无 pdflush/kupdate 风格的后台 writeback 与 dirty 回压；swap cache/pageout/pagein 与 vmscan 的 anon reclaim 联动需要后续补齐。

## 2. 新 Roadmap（v3，按里程碑）

### N0：基础同步原语与核心头文件（P0）

目标：补齐 Linux 2.6 核心同步原语与头文件，为后续模块提供基础支持。

子任务清单（可逐项完成）：
- [ ] 引入 `include/linux/spinlock.h`：实现基本的自旋锁语义。
- [ ] 引入 `include/linux/atomic.h`：实现原子操作语义。
- [ ] 完善 `include/linux/uaccess.h`：对齐用户空间访问语义。
- [ ] 引入 `include/linux/radix-tree.h` + `lib/radix-tree.c`：为 idr 实现做准备。
- [ ] 完善 `include/linux/list.h`：补齐更多 list/hlist 操作宏。

交付项：
- 核心同步原语：spinlock、atomic 等基本同步机制。
- 基础数据结构：radix-tree 实现，为后续 idr 替换做准备。
- 头文件对齐：关键头文件与 Linux 2.6 语义对齐。

验收：
- 编译通过，无警告；基础同步原语可正常使用。

### N1：块层完善（bio → request → request_queue）（P1）

目标：完善块层实现，实现完整的 bio/request/request_queue 结构，支持 I/O 调度。

子任务清单（可逐项完成）：
- [ ] 完善 `include/linux/bio.h`：增强 bio 结构，支持更复杂的 I/O 操作。
- [ ] 完善 `include/linux/blk_request.h`：实现完整的 request 结构。
- [ ] 完善 `include/linux/blk_queue.h`：实现完整的 request_queue 结构。
- [ ] 实现 I/O 调度器框架：至少支持 noop 调度器。
- [ ] 完善 `block/blk-core.c`：实现完整的 `submit_bio` 和 `generic_make_request`。
- [ ] 完善 `drivers/block/ramdisk.c`：实现基于 request_queue 的 ramdisk 驱动。

交付项：
- 完整的块层结构：bio、request、request_queue 实现。
- I/O 调度器框架：支持基本的 I/O 调度功能。
- 块设备驱动接口：统一的块设备驱动接口。

验收：
- 块设备读写正常；I/O 调度器可正常工作。

### N2：buffer cache 完善（P2）

目标：完善 buffer cache 实现，与 page cache 深度集成，支持更高效的块级操作。

子任务清单（可逐项完成）：
- [ ] 完善 `fs/buffer.c`：增强 buffer_head 管理。
- [ ] 实现 buffer cache 与 page cache 的集成：支持 page 与多个 buffer_head 的映射。
- [ ] 完善 `bread/bwrite/ll_rw_block` 等块读写原语。
- [ ] 实现 buffer cache 的回收机制：与 vmscan 集成。

交付项：
- 完善的 buffer cache 实现：支持高效的块级操作。
- 与 page cache 的深度集成：实现 buffered I/O。
- 块读写原语：统一的块级操作接口。

验收：
- MinixFS 元数据读写性能提升；buffer cache 可正常回收。

### N3：内存管理完善（P3）

目标：完善内存管理子系统，实现更接近 Linux 2.6 的内存管理机制。

子任务清单（可逐项完成）：
- [ ] 替换 `lib/idr.c`：基于 radix-tree 实现真实的 idr。
- [ ] 完善 `mm/slab.c`：实现更接近 Linux 2.6 的 slab 分配器。
- [ ] 完善 `mm/page_alloc.c`：增强 buddy 分配器，支持迁移类型与统计。
- [ ] 完善 `mm/vmalloc.c`：增强 vmap 区域管理与页表映射。

交付项：
- 完善的内存分配器：buddy + slab 实现。
- 真实的 idr 实现：基于 radix-tree。
- 内存管理统计：更完善的内存使用统计。

验收：
- 内存分配性能提升；内存使用统计准确。

### N4：启动流程与 initcall 分级（P4）

目标：对齐 Linux 2.6 的启动流程，实现 initcall 分级机制。

子任务清单（可逐项完成）：
- [ ] 完善 `init/main.c`：对齐 Linux 2.6 的启动流程。
- [ ] 实现 initcall 分级：core_initcall、device_initcall 等。
- [ ] 完善 `include/linux/init.h`：定义 initcall 相关宏。
- [ ] 实现参数解析：支持内核启动参数。

交付项：
- 对齐的启动流程：与 Linux 2.6 启动流程一致。
- initcall 分级：支持不同级别的初始化调用。
- 参数解析：支持内核启动参数。

验收：
- 启动流程与 Linux 2.6 对齐；initcall 分级正常工作。

### N5：时间管理与信号处理（P5）

目标：完善时间管理与信号处理机制，与 Linux 2.6 语义对齐。

子任务清单（可逐项完成）：
- [ ] 完善 `kernel/time.c`：实现更完整的时间管理。
- [ ] 完善 `kernel/timer.c`：实现更完整的定时器子系统。
- [ ] 完善 `kernel/signal.c`：实现更完整的信号处理机制。

交付项：
- 完善的时间管理：支持更精确的时间测量。
- 完善的定时器子系统：支持各种定时器需求。
- 完善的信号处理：与 Linux 2.6 信号语义对齐。

验收：
- 时间管理正常；信号处理与 Linux 2.6 语义一致。

### N6：写回线程模型 + swap/anon reclaim（P6）

目标：实现后台 writeback 与 swap 功能，与 Linux 2.6 语义对齐。

子任务清单（可逐项完成）：
- [ ] 实现后台 writeback：pdflush/kupdate 风格的后台写回。
- [ ] 实现 dirty 回压：balance_dirty_pages 最小版回压。
- [ ] 完善 `mm/swap.c`：实现 swap cache/pageout/pagein。
- [ ] 完善 `mm/vmscan.c`：实现 anon reclaim 与 OOM 决策。

交付项：
- 后台 writeback：支持脏页的后台写回。
- swap 功能：支持 swap in/out。
- 内存回收：支持 anon 页回收与 OOM 决策。

验收：
- 后台 writeback 正常工作；swap 功能可正常使用。

## 3. 强制回归基线（v3）

每次合并前必须满足：
- `make -j$(nproc)` 通过
- `make smoke-512` 通过（完整压力路径）
- `make smoke-128` 通过（低内存退化路径）
- 新增专项测试必须能在 30s 内稳定复现（避免偶现）

## 4. 非目标（仍然刻意延后）

- 网络栈、security、复杂 IPC。
- 完整 SMP/NUMA、高端内存（highmem/PAE）与复杂 I/O 调度器策略（先做框架）。
- journaling FS（ext3/ext4）与复杂块设备（DM/LVM/multipath）。