---
name: "linux-alignment"
description: "强制 Lite Kernel 与 Linux 概念/命名/语义/流程对齐。新增或修改任何内核代码（结构体/函数/接口/路径）时必须调用；不一致必须写明原因与差异清单。"
---

# Linux Alignment

本技能用于把 Lite Kernel 的所有改动约束在“Linux 可对应”的概念体系里：**命名、对外语义、关键流程/生命周期**都要能在 Linux（优先 Linux 2.6）找到对应。允许做简化实现，但**禁止发明新概念/新词汇**；确实无法一致时必须明确给出原因与差异说明。

## 何时必须调用

满足任意一条即必须调用：

1. 新增/重命名任何：结构体、函数、模块目录、sysfs/procfs 节点、uevent 字段、ioctl/ABI。
2. 修改 drivers/base、kobject、sysfs、VFS/namespace、block/tty/pci 等关键模型或对外行为。
3. 引入任何“状态机/flag/术语”用于表达模型状态（例如“ready/inited”之类）。

## 输出要求（必须给出）

1. **Linux 对应项**：给出 Linux 2.6 的概念/符号/路径对应（至少 1 个）。
   - 优先引用本仓库 vendored 的 `linux2.6/` 代码路径或注释名词。
   - 也可以引用 Linux 术语（例如：`kobject`、`kset`、`bus_type`、`device`、`device_driver`、`class`、`sysfs_ops`、`attribute_group`、`uevent`、`modalias`、`initcall`、`refcount/kref`）。
2. **一致性结论**：逐项说明以下三类是否一致：
   - `Naming`：命名是否为 Linux 术语/是否能在 Linux 找到对应。
   - `Semantics`：对外可见行为（sysfs/procfs/uevent/用户态 ABI）是否一致。
   - `Flow/Lifetime`：关键流程（initcall 顺序、引用计数、对象释放、sysfs 节点生命周期）是否一致。
3. **不一致必须说明**：任何“不一致”必须写出：
   - `Why`：为什么不能一致（资源/简化/缺子系统/历史包袱）。
   - `Impact`：对外行为差异、风险（兼容/安全/稳定性）。
   - `Plan`：如何收敛（或明确不收敛的长期理由）。

## 执行清单（逐条过）

### A. 术语/命名

1. 新增的名词必须能在 Linux 里找到对应概念；否则改名为 Linux 术语。
2. 结构体/函数名优先对齐 Linux driver core 常见命名（示例：`device_add`/`device_del`、`driver_register`、`bus_register`、`kobject_init`、`kset_init`、`sysfs_ops`）。
3. 禁止用“实现细节词”冒充概念（例如用字符串 type 表达根设备语义）。

### B. 对外语义（sysfs/procfs/uevent/ABI）

1. sysfs 目录树必须遵循 Linux 的“真实树 + 视图树”思路：
   - `/sys/devices`：真实拓扑树
   - `/sys/bus/*/devices`、`/sys/class/*`：视图（常为 symlink）
2. uevent 字段命名必须用 Linux 通用字段（`ACTION/DEVPATH/SUBSYSTEM/MODALIAS/DEVNAME/MAJOR/MINOR`）。
3. 任何对外路径/字段变化都必须更新 smoke/文档，并解释与 Linux 的对应关系。

### C. 流程/生命周期（Flow/Lifetime）

1. init 顺序必须能映射到 Linux：`early/core/subsys/fs/device/late initcall` 的语义，不要再发明额外“ready flag”。
2. 引用计数必须可解释且可追踪：谁持有谁、何时释放、是否可能 UAF/泄漏。
3. sysfs 节点若缓存 inode/dentry，必须评估其与 owner object 生命周期的绑定策略（Linux sysfs/kernfs 的对应语义）。

## 简化实现的允许边界（必须注明）

允许简化，但要显式标注为“简化版 Linux 语义”，例如：

1. `bus.match` 策略可简化（不实现完整 modalias/udev），但字段/路径/术语必须仍使用 Linux 概念。
2. sysfs symlink 解析可简化（例如仅支持绝对路径），但要明确写出与 Linux 的差异与后续收敛点。

## 推荐工具/检查

1. 快速硬闸：`make check-vocab`（仅做已知坏词黑名单扫描，不能替代本技能的语义审查）。
2. 回归：`make -j4 && make smoke-512`。

## 评审输出模板（建议直接复制填充）

```text
Linux Alignment Report

Change scope:
- files: ...
- public surface: sysfs/procfs/uevent/ABI ...

Linux mapping:
- concept/symbol/path: ...

Consistency:
- Naming: OK/DIFF -> ...
- Semantics: OK/DIFF -> ...
- Flow/Lifetime: OK/DIFF -> ...

If DIFF:
- Why:
- Impact:
- Plan:
```
