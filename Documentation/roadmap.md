# Lite OS 核心演进路线图

## 核心目标

我们的核心目标是构建一个功能完整、稳定可靠的操作系统内核，以 Linux 2.6 为蓝本，重点关注以下几个方面：

1. **块层/缓存/磁盘 FS（Minix）主链路**：确保从 NVMe 设备到 MinixFS 的完整链路
2. **设备驱动模型**：完善 PCI/PCIe/NVMe 支持
3. **Linux 2.6 语义对齐**：逐步实现与 Linux 2.6 一致的核心语义
4. **稳定性与可靠性**：确保系统稳定运行，无内存泄漏和崩溃
5. **可测试性**：完善测试框架，确保所有功能都能被有效测试

## 当前实现状态

### 已完成的核心功能

- **内存管理**：基本的页分配、虚拟内存、COW、rmap 等
- **文件系统**：ramfs、procfs、sysfs、devtmpfs、MinixFS（支持读写）
- **块设备**：基本的 bio/request 结构，ramdisk 驱动
- **设备驱动模型**：PCI/PCIe/NVMe 枚举，支持 NVMe 设备检测
- **系统调用**：基本的文件操作、进程管理等
- **测试框架**：`smoke` 测试程序，集成了多种测试用例

### 与 Linux 2.6 的主要差距

1. **基础同步原语**：缺少 spinlock、atomic 等核心同步机制
2. **块层实现**：缺少完整的 request_queue 与 I/O 调度器
3. **buffer cache**：与 page cache 集成不完整
4. **内存管理**：缺少 radix-tree、更完善的 slab 实现
5. **启动流程**：缺少 initcall 分级、参数解析等
6. **时间管理**：缺少完整的 timer 子系统
7. **信号处理**：信号处理机制不完整
8. **写回线程与 swap**：缺少后台 writeback 与 swap 功能

## 演进路线图

### 阶段一：基础同步原语与核心头文件（1-2 周）

**目标**：补齐 Linux 2.6 核心同步原语与头文件，为后续模块提供基础支持。

**任务**：
- [ ] 实现 `spinlock.h`：基本的自旋锁语义
- [ ] 实现 `atomic.h`：原子操作语义
- [ ] 完善 `uaccess.h`：对齐用户空间访问语义
- [ ] 引入 `radix-tree.h` + `radix-tree.c`：为 idr 实现做准备
- [ ] 完善 `list.h`：补齐更多 list/hlist 操作宏

**验收标准**：
- 核心同步原语可正常使用
- 编译通过，无警告

### 阶段二：块层完善（2-3 周）

**目标**：完善块层实现，实现完整的 bio/request/request_queue 结构，支持 I/O 调度。

**任务**：
- [ ] 完善 `bio.h`：增强 bio 结构，支持更复杂的 I/O 操作
- [ ] 完善 `blk_request.h`：实现完整的 request 结构
- [ ] 完善 `blk_queue.h`：实现完整的 request_queue 结构
- [ ] 实现 I/O 调度器框架：至少支持 noop 调度器
- [ ] 完善 `block/blk-core.c`：实现完整的 `submit_bio` 和 `generic_make_request`
- [ ] 完善 `drivers/block/ramdisk.c`：实现基于 request_queue 的 ramdisk 驱动

**验收标准**：
- 块设备读写正常
- I/O 调度器可正常工作
- 测试用例通过

### 阶段三：buffer cache 完善（1-2 周）

**目标**：完善 buffer cache 实现，与 page cache 深度集成，支持更高效的块级操作。

**任务**：
- [ ] 完善 `fs/buffer.c`：增强 buffer_head 管理
- [ ] 实现 buffer cache 与 page cache 的集成：支持 page 与多个 buffer_head 的映射
- [ ] 完善 `bread/bwrite/ll_rw_block` 等块读写原语
- [ ] 实现 buffer cache 的回收机制：与 vmscan 集成

**验收标准**：
- MinixFS 元数据读写性能提升
- buffer cache 可正常回收
- 测试用例通过

### 阶段四：内存管理完善（2-3 周）

**目标**：完善内存管理子系统，实现更接近 Linux 2.6 的内存管理机制。

**任务**：
- [ ] 替换 `lib/idr.c`：基于 radix-tree 实现真实的 idr
- [ ] 完善 `mm/slab.c`：实现更接近 Linux 2.6 的 slab 分配器
- [ ] 完善 `mm/page_alloc.c`：增强 buddy 分配器，支持迁移类型与统计
- [ ] 完善 `mm/vmalloc.c`：增强 vmap 区域管理与页表映射

**验收标准**：
- 内存分配性能提升
- 内存使用统计准确
- 测试用例通过

### 阶段五：启动流程与 initcall 分级（1-2 周）

**目标**：对齐 Linux 2.6 的启动流程，实现 initcall 分级机制。

**任务**：
- [ ] 完善 `init/main.c`：对齐 Linux 2.6 的启动流程
- [ ] 实现 initcall 分级：core_initcall、device_initcall 等
- [ ] 完善 `include/linux/init.h`：定义 initcall 相关宏
- [ ] 实现参数解析：支持内核启动参数

**验收标准**：
- 启动流程与 Linux 2.6 对齐
- initcall 分级正常工作
- 内核启动参数可正常解析

### 阶段六：时间管理与信号处理（1-2 周）

**目标**：完善时间管理与信号处理机制，与 Linux 2.6 语义对齐。

**任务**：
- [ ] 完善 `kernel/time.c`：实现更完整的时间管理
- [ ] 完善 `kernel/timer.c`：实现更完整的定时器子系统
- [ ] 完善 `kernel/signal.c`：实现更完整的信号处理机制

**验收标准**：
- 时间管理正常
- 信号处理与 Linux 2.6 语义一致
- 测试用例通过

### 阶段七：写回线程模型 + swap/anon reclaim（2-3 周）

**目标**：实现后台 writeback 与 swap 功能，与 Linux 2.6 语义对齐。

**任务**：
- [ ] 实现后台 writeback：pdflush/kupdate 风格的后台写回
- [ ] 实现 dirty 回压：balance_dirty_pages 最小版回压
- [ ] 完善 `mm/swap.c`：实现 swap cache/pageout/pagein
- [ ] 完善 `mm/vmscan.c`：实现 anon reclaim 与 OOM 决策

**验收标准**：
- 后台 writeback 正常工作
- swap 功能可正常使用
- 内存回收机制正常工作
- 测试用例通过

## 测试与验证

### 强制回归基线

每次合并前必须满足：
- `make -j$(nproc)` 通过
- `make smoke-512` 通过（完整压力路径）
- `make smoke-128` 通过（低内存退化路径）
- 新增专项测试必须能在 30s 内稳定复现（避免偶现）

### 测试覆盖范围

- **内存管理**：页分配、虚拟内存、COW、rmap 等
- **文件系统**：ramfs、procfs、sysfs、devtmpfs、MinixFS 等
- **块设备**：ramdisk、NVMe 等
- **设备驱动**：PCI/PCIe、NVMe 等
- **系统调用**：文件操作、进程管理、内存管理等
- **稳定性**：长时间运行无崩溃、无内存泄漏

## 非目标（暂时延后）

- 网络栈、security、复杂 IPC
- 完整 SMP/NUMA、高端内存（highmem/PAE）与复杂 I/O 调度器策略
- journaling FS（ext3/ext4）与复杂块设备（DM/LVM/multipath）