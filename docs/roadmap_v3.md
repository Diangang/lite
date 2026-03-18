# Lite OS → 32-bit Linux-like Kernel Roadmap (v3)

目标：把 Lite OS 演进为 **32 位、monolithic、Linux-like** 内核。用户态/内核态严格分离；Trap/调度/MM/VFS 语义可扩展；以 **procfs/sysfs** 为可观测与自描述基础；最终进入块设备与真实存储栈。

本版本定位：基于当前代码全量现状重新梳理，把“已完成/进行中/缺口”拆清，并给出更清晰的后续推进顺序与验收标准。

## 0. 当前实现基线（v3）

已具备：

- **Trap/调度地基**：syscall trap gate、不隐式关中断；TSS.esp0 per-task 更新；抢占调度骨架与 `/proc/sched` 统计。
- **阻塞与等待**：waitqueue + `task_wait`，shell 输入阻塞读闭环。
- **内存管理（PMM/VMM/mm/VMA）**：
  - PMM 位图分配器；VMM 0~128MB 恒等映射；per-task page directory。
  - `mm_struct` 已引入，VMA 统一驱动缺页与权限。
  - `brk` 最小版本与用户态缺页按需分配。
  - 退出回收基于 VMA 释放用户页与页表页（单一路径）。
- **I/O ABI 收敛**：
  - `open/read/write/close` 风格 syscall；fdtable per-task；fd=0/1/2 绑定 `/dev/console`。
- **可观测性**：
  - procfs：`/proc/tasks /proc/sched /proc/irq /proc/maps /proc/self/maps /proc/<pid>/maps /proc/meminfo /proc/<pid>/stat`。
  - sysfs：`/sys/kernel/version /sys/kernel/uptime /sys/devices/*`。
- **文件系统雏形**：
  - initrd（只读 fs_node 模型）、devfs、procfs、sysfs。
  - rootfs 为“拼接根”（不是 mount 语义）。
  - 最小 `file` 对象与 fdtable 对齐，offset 语义统一。

未对齐的关键缺口：

- **VFS 语义仍不完整**：缺 super_block / inode / dentry / mount；路径解析仍是 fs_node + 拼接根，未形成 mount tree。
- **命名空间缺失**：无 cwd、无相对路径、无 mount 表，`/proc` `/sys` `/dev` 只是 rootfs 分发逻辑。
- **kthread/mm 语义未完全分离**：kernel thread 仍持有 mm（需要明确 “user 必有 / kthread 可无”）。

## 1. v3 原则（新增约束）

- **对象模型优先**：VFS 先补齐对象关系（inode/dentry/sb/mount），再进入块设备与磁盘 FS。
- **路径解析必须与 mount 语义一致**：避免继续依赖硬编码 rootfs。

## 2. Phase D（v3 主线）：VFS 对象化 + Mount 语义

### D1. VFS 核心对象最小集

目标：
- 引入 `super_block`、`inode`、`dentry`、`file`、`mount` 的最小闭环。
- fdtable 绑定 VFS file；`open/read/write/close` 统一走 VFS 层。

验收标准：
- `cat /proc/meminfo`、`cat /sys/kernel/version`、`cat /dev/console` 走统一 VFS 路径（无 syscall 层特判）。

### D2. Mount tree 与命名空间

目标：
- 引入 mount 表，支持把 initrd/procfs/sysfs/devfs 挂载到 `/` 下的固定路径。
- 替换 `rootfs_make` 拼接根逻辑。

验收标准：
- `/proc` `/sys` `/dev` 来自 mount tree，而不是 rootfs 分发逻辑。
- 能把一个伪 fs 挂到 `/mnt` 证明机制泛化。

### D3. 路径解析语义补齐

目标：
- 支持 `cwd` 与相对路径；支持 `.` / `..` 与多重 `/` 归一化。
- shell 的 `ls/cat/run` 使用统一 VFS 路径解析。

验收标准：
- `cd` 后 `cat readme.txt` 可在 cwd 下解析成功。
- `/proc/self/maps` 与 `proc/self/maps` 行为一致。

## 3. Phase C（补齐）：mm 语义完善

### C4. kthread 与 user mm 分离

目标：
- kernel thread 可无 mm 或绑定 kernel_mm；user 任务必须有独立 mm。

验收标准：
- `/proc/<pid>/maps` 对 kernel thread 输出空或 kernel-only 标识。

### C5. /proc/<pid>/* 扩展

目标：
- 新增 `/proc/<pid>/cmdline`、`/proc/<pid>/status`、`/proc/<pid>/fd/*`。

验收标准：
- `/proc/<pid>/fd/0..2` 指向 `/dev/console`，能体现 fdtable 语义。

## 4. Phase E（不在本轮推进）

- Block layer / cache / 磁盘 FS 暂不启动，必须等 Phase D 完成后再进入。
