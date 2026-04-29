# Linux 2.6 全仓库固定执行清单

## 文档定位
- 这是后续 Linux 2.6 对齐工作的固定主清单。
- 目标不是重新发散 brainstorm，而是把“已经完成了什么、现在做到哪里、后面严格按什么顺序做”固定下来。
- 后续执行顺序以本文为准；阶段细节和显式 `DIFF` 仍以 `Documentation/Linux26-Subsystem-Alignment.md` 为权威明细。

## 执行规则
- 固定顺序执行，除非用户明确批准，不得跳项、并项、换项。
- 每一轮改动都必须先过 `linux-alignment` 的 Gate 0/Gate 1：
  - 先读 Linux
  - 再读 Lite
  - 先给 ledger
  - 只收敛一个差异点
- 严禁“顺手优化”“顺手统一”“顺手扩展”。
- 严禁新增 Linux 中没有直接对应的 Lite 自创层。
- 明确冻结：不再新增 block 的 sysfs 节点；block 方向只做 Linux 已有边界内的落位和语义收敛。

## 状态标记
- `DONE`：本阶段已完成，可作为后续工作的基线。
- `ACTIVE`：当前主线，后续优先只做这一项，直到收敛。
- `PENDING`：已排队，但未进入执行窗口。
- `HOLD`：明确存在差距，但受前置依赖约束，暂不展开。

## 已完成基线

### A. 已完成的阶段性基线
- `DONE` `arch/x86` Stage 0-13 基线：x86 启动/中断/APIC 占位分层已经建立，剩余差距主要集中在 LAPIC/IOAPIC/SMP 真实运行语义。
- `DONE` `kernel core` Stage 0-14 基线：`sched/fork/exit/wait/signal` 已完成基础 Linux 形状收敛，剩余差距主要集中在 SMP 安全、task ref、waitqueue 锁、完整 signal 语义。
- `DONE` `lib/` Stage 0-4 基线：`kref/idr/printk` 等 Linux 术语边界已经建立，剩余差距集中在非原子 refcount、真实 radix-tree idr、少量 Lite convenience helper。

### B. 已完成的落位收敛
- `DONE` x86 启动入口落位：`i386_start_kernel()` 已迁到 `arch/x86/kernel/head32.c`，与 Linux 对应文件对齐。
- `DONE` block queue sysfs 落位：已有的 `nr_requests` show/store 已迁到 `block/blk-sysfs.c`，停止继续扩展新的 block sysfs 节点。
- `DONE` NVMe host 目录收敛：NVMe host 主实现已迁到 `drivers/nvme/host/`，不再维持旧的单文件 NVMe 实现放置方式。
- `DONE` smoke harness 退出时序：`smoke-512`/`smoke-128` 已避免“测试 OK 但 QEMU 长时间不退”的误判。

## 固定执行顺序

### 1. NVMe host 最终收敛
- 状态：`ACTIVE`
- 范围：
  - `drivers/nvme/host/pci.c`
  - `drivers/nvme/host/nvme.h`
- 本项只做：
  - 清理剩余 `Placement: DIFF`
  - 清理过渡层和桥接头
  - 收敛 `struct`/`function` 落位
  - 在不扩 scope 的前提下稳定 `smoke-512`
- 本项不做：
  - 不扩展新的 block sysfs
  - 不顺手改 block 层大框架
  - 不引入 Linux 中不存在的新 NVMe 抽象层
- 进入下一项的完成条件：
  - NVMe host 范围内账本无关键 `Placement: DIFF`
  - `make -j4`
  - `make smoke-512`
  - `make smoke-128`

### 2. 启动流程与 initcall 分级
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/init/main.c`
  - `linux2.6/include/linux/init.h`
- 目标：
  - 收敛 `start_kernel()` 主流程
  - 补齐 initcall 分级边界
  - 收敛参数解析与初始化顺序
- 本项只做启动链路，不混入驱动或块层语义调整。

### 3. 基础同步原语与核心头文件
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/include/linux/spinlock.h`
  - `linux2.6/include/linux/atomic.h`
  - `linux2.6/include/linux/uaccess.h`
- 目标：
  - 建立后续 SMP-safe 收敛的基础语义
  - 为 waitqueue、tasklist、driver core、block 层提供真实同步基石
- 说明：
  - 这是很多 `PARTIAL DIFF` 的前置依赖，不完成它，后续很多模块只能停留在 UP-only 形态。

### 4. block 核心层
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/block/blk-core.c`
  - `linux2.6/block/blk-sysfs.c`
  - `linux2.6/include/linux/blkdev.h`
  - `linux2.6/include/linux/blk_types.h`
- 目标：
  - 收敛 `bio/request/request_queue` 的 Linux 形状
  - 收敛 `submit_bio` / `generic_make_request` 语义
  - 建立最小 I/O scheduler 框架
- 明确冻结：
  - 不新增 block sysfs 节点
  - 只处理核心数据结构、提交流程和调度边界

### 5. buffer cache / page cache / writeback 主链路
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/fs/buffer.c`
  - `linux2.6/mm/filemap.c`
  - `linux2.6/mm/page-writeback.c`
- 目标：
  - 收敛 `buffer_head` 相关边界
  - 建立 page cache 与块层/写回的真实耦合
  - 为 MinixFS 和块设备 I/O 语义提供稳定基线

### 6. mm 子系统第二轮收敛
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/mm/page_alloc.c`
  - `linux2.6/mm/slab.c`
  - `linux2.6/mm/vmalloc.c`
  - `linux2.6/lib/radix-tree.c`
  - `linux2.6/lib/idr.c`
- 目标：
  - `page_alloc` / `slab` / `vmalloc` 继续向 Linux 形状靠拢
  - 引入 radix-tree 支撑真实 `idr`
  - 收敛当前 `lib/` 和 `mm/` 中遗留的实现型 `DIFF`

### 7. kernel core 剩余 `PARTIAL DIFF`
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/kernel/sched.c`
  - `linux2.6/kernel/fork.c`
  - `linux2.6/kernel/exit.c`
  - `linux2.6/kernel/signal.c`
- 本项聚焦：
  - `tasklist_lock`
  - per-CPU `current/rq/need_resched`
  - task lifetime / refs
  - waitqueue locking
  - signal model 的必要收敛
- 说明：
  - 该项必须排在同步原语之后，否则只能继续停留在文档化 `DIFF`。

### 8. driver core / sysfs / devtmpfs
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/drivers/base/*`
  - `linux2.6/fs/sysfs/*`
- 目标：
  - 继续收敛 device / driver / bus / class / kobject / sysfs 生命周期
  - 收敛 parent-child、bind-unbind、uevent 和目录层级
- 说明：
  - 只收敛 Linux 现有模型，不做 Lite 自定义管理层。

### 9. tty / console / serial
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/drivers/tty/*`
  - `linux2.6/kernel/printk.c`
- 目标：
  - 收敛 tty core / line discipline / serial core / console 输出边界
  - 减少遗留 `printf` 风格 convenience 路径

### 10. PCI / SCSI / virtio 存储外围
- 状态：`PENDING`
- Linux 参考：
  - `linux2.6/drivers/pci/*`
  - `linux2.6/drivers/scsi/*`
  - `linux2.6/drivers/virtio/*`
- 目标：
  - 在 block 主链路稳定后，再继续收敛外围总线与存储驱动边界
  - 避免在 block 层未稳之前同时改太多外围驱动

### 11. arch/x86 APIC / IOAPIC / SMP 真实运行语义
- 状态：`HOLD`
- Linux 参考：
  - `linux2.6/arch/x86/kernel/apic/*`
  - `linux2.6/arch/x86/kernel/smp.c`
  - `linux2.6/arch/x86/include/asm/irq_vectors.h`
- 目标：
  - LAPIC timer
  - IPI send path
  - IOAPIC routing
  - SMP 中断与调度协同
- 为什么先 HOLD：
  - 它依赖同步原语、kernel core per-CPU 形状、waitqueue/task lifetime 等前置工作；
  - 目前可以保留为显式 `DIFF`，不应提前展开导致全仓库再次发散。

## 明确不做
- 不新增 block sysfs 节点。
- 不因为局部方便而把函数/结构体放到 Linux 对应文件之外。
- 不把文档中的 `HOLD` 项偷偷提前实现一部分。
- 不在未完成当前项之前跨到下一个大模块。
- 不为追求“更优雅”引入 Linux 里不存在的新中间层。

## 当前主线
- 当前唯一主线是“`1. NVMe host 最终收敛`”。
- 后续对话如果没有用户明确改序，默认只围绕这一项推进。

## 变更规则
- 若需要调整清单顺序，必须满足两点：
  - 先在对话中说明为什么当前顺序阻塞了后续工作
  - 经用户明确批准后再修改本文
