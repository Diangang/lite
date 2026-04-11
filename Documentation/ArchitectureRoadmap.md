# Lite Kernel Architecture Roadmap

本文档基于当前 Lite Kernel 的静态结构分析整理，目标不是先补更多功能，而是先把系统的对象模型、接口边界、目录语义和模块角色收敛到更接近 Linux 的方式。

当前判断：
- 命名和目录划分已经大体接近 Linux。
- 主要问题不是“文件放错目录”，而是“职责混层”。
- 后续如果继续直接堆功能，复杂度会越来越集中在错误的层次上。

## 1. 当前总体差异

### 1.1 VFS
- `file_operations` 当前同时承载了文件 I/O 和目录树操作。
- `create/finddir/unlink/rmdir` 更接近 Linux 中 `inode_operations` 的职责。
- 当前实现虽然能跑，但 inode 和 file 的角色边界不清。

### 1.2 MM / Page Cache / Block
- `mm/filemap.c` 里混入了块设备专用的 `blockdev_readpage/writepage` 逻辑。
- `block_device` 结构体中仍然混有 ramdisk 后端字段。
- page cache、block core、具体块设备后端三层还没有完全拆开。

### 1.3 Kernel / MM
- `kernel/fork.c` 中的 `mm_clone_cow()` 直接处理页表、COW 和 rmap。
- 这说明 `kernel/` 侵入了 `mm/` 内部语义。
- 更合理的方向是让 `fork` 调用 `mm/` 层的 helper，而不是自己理解地址空间细节。

### 1.4 Scheduler
- 当前调度模型是全局任务链表 + round-robin，适合作为最小实现。
- 主要问题不是算法太简单，而是对象拆分不够：
  - `task_struct` 偏胖
  - `wait queue` 仍然依赖 `task->wait_next`
  - `sched.c` 集中了较多 glue 逻辑

### 1.5 Driver Core
- `struct device` 中仍然带有较多 devnode 语义。
- device core、class、devtmpfs、sysfs 的边界还可以继续收敛。

### 1.6 Init Policy
- 启动阶段的挂载和设备选择策略仍然硬编码在 `init/main.c`。
- 这类逻辑更适合作为 boot 参数或独立策略层。

## 2. 总体重构原则

### 2.1 先收边界，再补能力
- 先把对象放到正确的层。
- 再在正确的层上继续补功能。

### 2.2 小步重构
- 每个阶段单独完成。
- 每阶段必须保证系统仍可构建、启动、运行 smoke。

### 2.3 Linux 对齐优先级
- 优先对齐概念、术语、接口角色。
- 允许实现简化，但不继续发明新的跨层抽象。

## 3. 分阶段计划

## Phase 1: VFS 对象模型收敛

### 目标
- 把目录树操作从 `file_operations` 中拆出去。
- 建立更接近 Linux 的 `inode_operations` / `file_operations` 分工。

### 主要动作
- 在 `struct inode` 中引入 `i_op` 和 `i_fop`。
- 把 `create/lookup/mkdir/unlink/rmdir` 从 `file_operations` 迁移到 `inode_operations`。
- 让 `vfs_open()`、`vfs_mkdir()`、`namei` 改走 inode 语义。
- 为 `ramfs`、`minixfs`、`procfs`、`sysfs`、`devtmpfs` 补最小 `inode_operations`。

### 收益
- 目录项管理与文件读写职责分开。
- 后续权限、truncate、link、rename 才有正确落点。

### 验收
- `/`、`/proc`、`/sys`、`/dev`、`/mnt` 基本访问无回归。
- `open(O_CREAT)`、`mkdir`、`unlink` 路径不再依赖 `file_operations` 私有扩展。

## Phase 2: Page Cache 与 Block 分层

### 目标
- 把通用 page cache 和块设备专用读写 glue 分开。

### 主要动作
- 保留 `address_space` / `address_space_operations` 在 `mm/`。
- 把 `blockdev_readpage/writepage` 从 `mm/filemap.c` 迁移到更合适的位置。
- 明确谁提供 page cache 机制，谁提供块设备后端 I/O。

### 收益
- `mm/filemap` 回到通用缓存层角色。
- block 设备只是具体用户，而不是 filemap 的内置特例。

### 验收
- 普通文件和块设备读写行为保持现状。
- `mm/filemap.c` 不再直接携带 blockdev 专用策略。

## Phase 3: Block Core 与具体后端解耦

### 目标
- 让 `block_device` 成为通用抽象，而不是 ramdisk 的混合结构。

### 主要动作
- 从 `struct block_device` 中抽离 `data/reads/writes/bytes_*` 等后端字段。
- 为 ramdisk 新建私有结构，例如 `ramdisk_device`。
- 统一通过 `request_queue->queuedata` 进入具体块设备实现。

### 收益
- NVMe、ramdisk、后续 loop/dm 可以共用同一块层抽象。

### 验收
- `ramdisk` 与 `nvme` 同时通过 `smoke`。
- `block_device` 仅表达通用块设备语义。

## Phase 4: Kernel 与 MM 边界收敛

### 目标
- 把地址空间复制和页表/COW 细节收回 `mm/`。

### 主要动作
- 将 `kernel/fork.c` 中的 `mm_clone_cow()` 迁移或拆分到 `mm/`。
- 为 `fork` 提供 `dup_mm` / `dup_mmap` 风格 helper。
- `fork.c` 只负责任务对象创建和进程级资源复制。

### 收益
- `kernel/` 负责 task/process。
- `mm/` 负责地址空间和虚存细节。

### 验收
- `fork/exec/waitpid` 保持工作。
- `fork.c` 不再直接理解页表/COW/rmap 细节。

## Phase 5: Scheduler 与 Wait Queue 抽象清理

### 目标
- 不先升级调度算法，先把调度器对象模型理顺。

### 主要动作
- 为 wait queue 引入更独立的等待项概念。
- 减少 `task_struct` 中对 wait queue 链接字段的直接承载。
- 视情况引入最小 `runqueue` 抽象，而不是直接扫全局任务链表。

### 收益
- 调度、睡眠、唤醒、等待关系更清楚。
- 后续扩 signal wakeup、超时等待时更容易。

### 验收
- `sleep/yield/waitpid/exit` 行为保持。
- wait queue 不再完全依赖 `task->wait_next`。

## Phase 6: Driver Core 瘦身

### 目标
- 让 `struct device` 更接近 Linux 通用 device core。

### 主要动作
- 逐步弱化 `dev_major/dev_minor/devnode_name/type` 这类过于具体的字段耦合。
- 强化 `class`、`devt`、`devnode`、`kobj_type`、`sysfs_ops` 的职责边界。
- 继续收敛 `device core <-> sysfs <-> devtmpfs` 的接口。

### 收益
- generic device core 不再承载过多节点层实现细节。
- 驱动模型后续更容易继续向 Linux 对齐。

### 验收
- `sysfs`、`devtmpfs`、`uevent` 无回归。
- `struct device` 角色更通用。

## Phase 7: Init 策略参数化

### 目标
- 把“选择哪块盘挂载什么”的策略从启动主流程中拆出去。

### 主要动作
- 引入 boot 参数或最小挂载策略模块。
- `prepare_namespace()` 只负责执行，不负责决定。
- rootfs、`/mnt`、NVMe/ramdisk fallback 全部参数化。

### 收益
- `init` 保持 orchestration 角色。
- 设备选择策略不再散落在启动代码里。

### 验收
- 不修改 `init/main.c` 逻辑主体即可切换挂载设备选择。

## Phase 8: 存储能力深化

### 目标
- 在层次理顺后，再继续增强 NVMe、block、filesystem。

### 主要动作
- NVMe:
  - namespace 遍历
  - 64-bit capacity
  - 后续再考虑中断、多队列
- block:
  - 更像 Linux 的 request 生命周期
- minixfs:
  - 完整化 truncate/link/unlink/目录操作

### 收益
- 功能增强建立在正确的结构边界上，而不是继续堆在过渡实现里。

### 验收
- 双 NVMe 场景持续稳定：
  - 一个文件系统盘
  - 一个裸盘

## 4. 推荐执行顺序

1. Phase 1: VFS 对象模型收敛
2. Phase 2: Page Cache 与 Block 分层
3. Phase 3: Block Core 与具体后端解耦
4. Phase 4: Kernel 与 MM 边界收敛
5. Phase 5: Scheduler 与 Wait Queue 抽象清理
6. Phase 6: Driver Core 瘦身
7. Phase 7: Init 策略参数化
8. Phase 8: 存储能力深化

## 5. 每阶段统一回归要求

- `make -j4`
- `smoke`
- 双 NVMe 验证：
  - `nvme0n1` 用于文件系统
  - `nvme1n1` 用于裸盘读写

## 6. 一句话总结

Lite Kernel 当前的关键任务不是继续快速加功能，而是先把：
- `inode` 和 `file`
- `page cache` 和 `block`
- `kernel` 和 `mm`
- `device core` 和 `devnode/sysfs`

这些边界收干净。只有这样，后续新增能力才会落在正确的位置上。
