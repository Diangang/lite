---
name: "linux-alignment"
description: "强制 Lite Kernel 与 Linux 概念/命名/语义/流程对齐。新增或修改任何内核代码（结构体/函数/接口/路径）时必须调用；不一致必须写明原因与差异清单。"
---

# Linux Alignment

本技能用于把 Lite Kernel 的所有改动约束在“Linux 可对应”的概念体系里：**命名、对外语义、关键流程/生命周期**都要能在 Linux 找到对应。允许做简化实现，但**禁止发明新概念/新词汇**；确实无法一致时必须明确给出原因与差异说明。

## 核心原则（强制）

### 1. Reference-First

任何设计、命名、流程调整、接口增删，**必须先参考 Linux 对应实现，再做 Lite 修改**。
禁止先凭经验/想象提出“演进方向”，再事后补 Linux 对应项。

### 2. Linux-First, Not Idea-First

如果 Linux 已有明确实现/命名/流程：
- 必须优先复用 Linux 的概念、命名、分层、执行顺序
- 不得先发明 Lite 自己的中间抽象，再说“以后向 Linux 收敛”

如果 Linux 没有明确对应：
- 必须明确写出“Linux 无直接对应”
- 必须停止继续“自由演进”
- 必须向用户说明差异，并在必要时先询问用户，而不是自行扩展

### 3. No Imagination-Driven Refactor

禁止以下行为：
- 根据“看起来更合理”自行发明新抽象
- 根据“也许以后会这样演进”擅自改造代码
- 用“更优雅”“更统一”“更像某种设计”替代 Linux 代码依据
- 未核对 Linux 代码，仅根据术语印象做改动

一句话：**没有 Linux 依据，就不要改；改了就必须能指出 Linux 对应代码。**

## 何时必须调用

满足任意一条即必须调用：

1. 新增/重命名任何：结构体、函数、模块目录、sysfs/procfs 节点、uevent 字段、ioctl/ABI。
2. 修改内核关键模型或对外行为（例如 driver core、kobject/sysfs、VFS/namespace、各类总线/设备模型等）。
3. 引入任何“状态机/flag/术语”用于表达模型状态（例如“ready/inited”之类）。

## 输出要求（必须给出）

0. **修改前引用依据**：在任何实际改动前，必须先给出本次要参考的 Linux 代码依据，至少包含：
   - Linux 文件路径
   - 对应函数/结构体/变量名
   - Lite 侧准备对齐的目标项
   - 若拿不出这些依据，禁止开始修改
1. **Linux 对应项**：给出 Linux 的概念/符号/路径对应（至少 1 个）。
   - 优先引用本仓库内的 Linux 参考树（若存在 vendored/reference 目录则以其为准）。
   - 也可以引用 Linux 术语（例如：`kobject`、`kset`、`bus_type`、`device`、`device_driver`、`class`、`sysfs_ops`、`attribute_group`、`uevent`、`modalias`、`initcall`、`refcount/kref`）。
2. **一致性结论**：逐项说明以下三类是否一致：
   - `Naming`：命名是否为 Linux 术语/是否能在 Linux 找到对应。
   - `Semantics`：对外可见行为（sysfs/procfs/uevent/用户态 ABI）是否一致。
   - `Flow/Lifetime`：关键流程（initcall 顺序、引用计数、对象释放、sysfs 节点生命周期）是否一致。
3. **不一致必须说明**：任何“不一致”必须写出：
   - `Why`：为什么不能一致（资源/简化/缺子系统/历史包袱）。
   - `Impact`：对外行为差异、风险（兼容/安全/稳定性）。
   - `Plan`：如何收敛（或明确不收敛的长期理由）。

## 执行闸门（强制）

### Gate 1. 先读 Linux，再计划

在修改任何相关代码前，必须先完成：
1. 读取 Lite 当前实现
2. 读取 Linux 对应实现
3. 列出“本次只改哪些点，不改哪些点”

若未完成这三步，禁止修改。

### Gate 2. 一次只收敛一个明确差异

每一轮修改必须只围绕**已在 Linux 中确认存在的一个差异点**展开，例如：
- 路径位置对齐
- 函数命名对齐
- create/remove 流程对齐
- 生命周期归属对齐

禁止一轮中混入“顺手优化”“顺手抽象”“顺手统一”这类没有 Linux 依据的演进。

### Gate 3. 每一步修改都要能回答 4 个问题

在修改前必须自检：
1. Linux 对应代码在哪里？
2. Lite 当前哪一段和它不一致？
3. 这一步改完以后，哪一点会更接近 Linux？
4. 这一步有没有引入 Linux 中不存在的新抽象/新术语/新流程？

如果第 4 条答案不是“没有”，默认禁止改。

### Gate 4. 无 Linux 对应则停止扩展

若想引入新 helper / 新结构 / 新命名，而 Linux 中没有直接对应：
- 默认禁止
- 除非只是最小桥接层，且必须明确标注：
  - Linux 对应缺失在哪里
  - 为什么这是临时桥接，而不是新模型
  - 后续如何消掉这层桥接

否则必须先询问用户，而不是自行决定。

### Gate 5. 修改现有代码时必须做范围穷举核查

如果用户要求“修改现有代码”而不是纯新增代码，必须先对**指定修改范围**做穷举核查。
这里的“范围”包括但不限于：
- 目标文件
- 同一文件内被影响的所有函数
- 同一文件内被影响的所有结构体
- 同一文件内被影响的所有全局变量/静态变量
- 相关头文件中的声明
- 相关目录/文件路径

必须逐项检查以下对象是否都能与 Linux 对应：
1. 每一个函数
2. 每一个结构体
3. 每一个全局变量/静态变量
4. 每一个导出符号
5. 每一个文件
6. 每一个目录路径

若其中任意一项无法对应 Linux：
- 默认禁止继续改
- 必须先写明该项的 Linux 对应缺失点
- 必须说明为什么不能直接对齐
- 必须说明是否需要继续向用户确认

禁止为了省事只检查“主要函数”或“核心流程”，而跳过其余命名/变量/文件路径。

### Gate 5.5. 必须做“职责分层/生命周期反模式”负向扫描

在完成常规逐项映射后，**必须额外执行一次反模式扫描**，专门检查那些“命名看起来像 Linux、但流程/生命周期明显不像 Linux”的问题。
这一步是强制项，不能因为代码“能编译、能跑 smoke”而跳过。

至少必须检查以下问题：
1. **init/probe/remove 职责是否混杂**
   - 是否在 `module_init` / `*_init()` 中直接完成本应属于 `probe()` 的实例注册、子设备注册、资源绑定、父子关系建立？
2. **core 注册与具体实例注册是否混杂**
   - 是否把“类型/总线/core/class 注册”与“具体实例 bring-up / add / publish / attach”混在同一入口里？
   - 这里的“实例级动作”不限于任何特定子系统 API，凡是创建具体对象、发布派生对象、建立父子关系、绑定资源、附着上下文、暴露节点、激活队列/端口/会话，均算实例级动作。
3. **是否存在按全局名字回捞对象再绑定**
   - 例如按名字回捞父设备、类对象、总线对象、会话对象、inode/dentry、队列对象，然后再建立关系。
4. **是否存在“先假定对象存在，再事后绑定”**
   - 例如先注册 core/驱动，再立即查找一个预期实例并手工挂接派生对象、视图对象、用户态可见节点或内部从属对象。
5. **子设备/派生设备是否在错误时机创建**
   - Linux 若通常在 `probe()` 成功后创建，Lite 不得放在模块初始化、总线初始化或无设备上下文的 helper 中提前创建。
6. **实例名是否被错误地当作语义主键**
   - 例如把任何“拼接出来的实例名/路径名/显示名”当成匹配键、父设备定位键、生命周期锚点，而不是使用 Linux 对应对象链路。
7. **阶段边界是否被打穿**
   - 是否在“注册阶段”做了“运行阶段”的工作，或在“发现阶段”做了“发布阶段”的工作，或在“解绑阶段”遗漏回收/撤销发布？
8. **派生视图对象是否脱离真实 owner 生命周期**
   - 例如 class 视图、sysfs 节点、devtmpfs 节点、tty/block/net 子对象、缓存对象、工作队列上下文，其创建/销毁是否真正跟随 owner 对象，而不是靠全局状态和字符串拼接维持？

若发现上述任意一项：
- 不得因为“命名已经对齐”而判定通过
- 必须在报告中记为 `Flow/Lifetime: DIFF`
- 必须给出 `Why/Impact/Plan`
- 若 Linux 中有明确对应修法，优先继续收敛，而不是留到“以后再看”

### Gate 6. 未完成证据与穷举核查时，禁止编辑代码

在以下两项都完成之前：
- `Reference-first evidence`
- `Exhaustive Range Audit`

禁止执行任何代码编辑，包括但不限于：
- 修改现有文件
- 新增 helper
- 重命名函数/变量/结构体
- 调整文件路径
- 删除旧实现

在该阶段唯一允许的动作只有：
1. 继续读取 Lite 当前实现
2. 继续读取 Linux 对应实现
3. 向用户澄清范围或确认差异
4. 输出逐项映射表

如果还没完成这两个前置步骤，就开始编辑代码，视为违反本技能。

### Gate 7. 未逐项映射则禁止提交修改

对本轮修改范围内的所有对象，必须逐项映射 Linux 对应项后，才允许提交代码修改结果。
这里的“对象”包括：
- 保留但被影响的函数
- 新增函数
- 改名函数
- 保留但被影响的结构体
- 新增结构体
- 改名结构体
- 保留但被影响的全局变量/静态变量
- 新增全局变量/静态变量
- 改名全局变量/静态变量
- 保留但被影响的文件
- 新增文件
- 改名文件
- 保留但被影响的目录
- 新增目录
- 改名目录

若其中任意一项没有 Linux 对应：
- 默认禁止提交修改
- 必须明确标记为 `NO_DIRECT_LINUX_MATCH`
- 必须写出 `Why/Impact/Plan`
- 必须说明是否需要先征求用户确认

## 执行清单（逐条过）

### A. 术语/命名

1. 新增的名词必须能在 Linux 里找到对应概念；否则改名为 Linux 术语。
2. 结构体/函数名优先对齐 Linux driver core 常见命名（示例：`device_add`/`device_del`、`driver_register`、`bus_register`、`kobject_init`、`kset_init`、`sysfs_ops`）。
3. 禁止用“实现细节词”冒充概念（例如用字符串 type 表达根设备语义）。
4. 变量命名也必须尽可能向 Linux 靠拢，尤其是全局变量、文件级静态变量、导出符号。
5. 若仓库内 Linux 参考树中已存在明确对应的全局/静态变量命名，优先直接复用同名或同序风格命名。
6. 若无法与 Linux 变量名完全一致，必须在评审说明里写出 `Why/Impact/Plan`，不能只解释结构体或函数名一致。
7. 禁止自行创造抽象名，例如：
   - `descriptor`
   - `manager`
   - `adapter`
   - `bridge`
   - `wrapper`
   除非 Linux 中有明确对应，或仅作为临时桥接层并已写明 `Why/Impact/Plan`。
8. 对于修改范围内的**现有**函数/结构体/变量，禁止因为“顺手重构更方便”而改成 Linux 中不存在的新名字。
9. 对于修改范围内的文件和目录路径，禁止因为“看起来更整洁”而重命名到 Linux 中找不到对应的位置。

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
4. 流程收敛必须优先对齐 Linux 的**实际调用顺序**，而不只是“功能看起来一样”。
5. 如果 Linux 是 `A -> B -> C`，Lite 不得擅自改成 `A -> X -> C`，除非：
   - `X` 是无法避免的最小桥接层
   - 已写明它对应 Linux 的哪一层缺失
   - 已写明后续如何移除 `X`
6. 必须区分 **type/core registration**、**enumeration/discovery**、**match/bind**、**instance bring-up**、**publish user-visible/derived objects**、**teardown/unregister** 这六个层次；若这些层次在同一函数中被揉杂，默认视为 `Flow/Lifetime: DIFF`。
7. 这条检查适用于所有 Linux-like 子系统，不限于驱动模型；至少包括但不限于：
   - `driver core` / `bus` / `class`
   - `fs` / `path lookup` / `namespace` / `sysfs` / `procfs` / `devtmpfs`
   - `block` / `net` / `tty` / `input`
   - `mm` / `reclaim` / `slab` / `rmap`
   - `arch` 侧控制器注册与实例启用路径
8. 对任意子系统，都必须核查：
   - “注册某类对象”是否只做类型/core 注册
   - “创建某个实例或派生对象”是否位于更接近 Linux 对应生命周期的时机
   - 父子关系、owner 关系、回收关系是否通过对象链路维护，而不是依赖全局名字、路径字符串或隐式顺序假设

## 简化实现的允许边界（必须注明）

允许简化，但要显式标注为“简化版 Linux 语义”，例如：

1. `bus.match` 策略可简化（不实现完整 modalias/udev），但字段/路径/术语必须仍使用 Linux 概念。
2. sysfs symlink 解析可简化（例如仅支持绝对路径），但要明确写出与 Linux 的差异与后续收敛点。
3. 简化只允许“少实现 Linux 的一部分”，不允许“额外发明 Linux 没有的一层模型”来代替。

## 修改流程模板（强制遵守）

每次修改必须按以下顺序执行：

1. **Locate Linux Reference**
   - 明确读出 Linux 文件、函数、关键变量
2. **Locate Lite Delta**
   - 指出 Lite 当前与 Linux 的具体差异点
3. **Exhaustive Range Audit**
   - 若是修改现有代码，必须列出本轮范围内的：
     - 函数
     - 结构体
     - 全局变量/静态变量
     - 文件
     - 目录
   - 并逐项说明其 Linux 对应
4. **Define This Step Only**
   - 明确本轮只收敛一个差异点
5. **Edit Against Reference**
   - 修改时逐项对照 Linux，不得自由发挥
6. **Re-check Delta**
   - 修改后再次说明：哪一点更接近 Linux，哪一点仍不同
7. **Run Regression**
   - 必须构建和回归
8. **Submit Mapping Ledger**
   - 提交结果前，必须给出本轮全部对象的逐项 Linux 映射账本

## 明确禁止

1. 禁止“自言自语式演进”：
   - 例如“下一步可能可以这样”“我觉得这样更统一”
   - 若无 Linux 依据，不得作为实际修改依据
2. 禁止“先改后找依据”
3. 禁止“为了抽象统一而抽象统一”
4. 禁止把 Lite 的临时 helper 包装成长期模型
5. 禁止把“语义类似”当作“已经对齐”
6. 禁止未核对 Linux 代码就声称“Linux 也是这样”
7. 禁止修改现有代码时只核查“主流程”，而忽略变量、结构体、文件、目录对应关系
8. 禁止为了简便，自定义变量、结构体、函数，除非 Linux 中有直接对应，或已明确标注为不可避免的最小桥接层
9. 禁止把“当前先这样，后面再收敛”作为缺少 Linux 对应时的默认借口
10. 禁止在未完成 `Reference-first evidence` 和 `Exhaustive Range Audit` 前开始任何编辑
11. 禁止提交时省略“逐项 Linux 映射账本”

## 推荐工具/检查

1. 构建清理（强制）：在运行任何编译/回归相关命令前，必须先执行一次 `make clean`，避免残余产物导致误判。
2. 快速硬闸：`make clean && make check-vocab`（仅做已知坏词黑名单扫描，不能替代本技能的语义审查）。
3. 回归：`make clean && make -j4 && make smoke-512`。
4. 反模式扫描（强制，至少执行一次）：
   - 搜索所有“按名字/路径查找对象”的 helper，例如 `find_.*_by_name\(`、`.*_find\(`、`lookup.*name`
   - 搜索所有“注册类型/core”的 API，例如 `.*register.*driver`、`.*register.*class`、`.*register.*bus`、`.*initcall`
   - 搜索所有“创建/发布实例或派生对象”的 API，例如 `.*register.*device`、`device_add\(`、`.*add_one.*`、`.*create.*`、`.*publish.*`
   - 搜索这些调用是否出现在 `module_init`、`*_init()`、总线/class/core 注册函数，而非更接近 Linux 对应实例生命周期的上下文
   - 搜索“类型/core 注册”与“实例创建/发布”是否在同一初始化函数中揉杂
5. 对任何命中项，必须继续读取对应 Linux 参考实现，不能只报 grep 结果。

## 评审输出模板（建议直接复制填充）

```text
Linux Alignment Report

Change scope:
- files: ...
- public surface: sysfs/procfs/uevent/ABI ...

Reference-first evidence:
- Linux file: ...
- Linux function/struct/variable: ...
- Lite target to align: ...
- This step only changes: ...

Exhaustive range audit:
- functions:
- structs:
- globals/statics:
- files:
- directories:
- each item's Linux mapping:
- anti-pattern scan:
  - mixed init/probe/remove responsibilities:
  - mixed registration/discovery/bind/bring-up/publish/teardown phases:
  - global name lookup based binding:
  - core registration + instance publish mixed in one init:
  - instance-name-or-path-as-key issues:
  - derived-object lifetime detached from owner:

Mapping ledger:
- kept functions -> Linux mapping:
- new functions -> Linux mapping:
- renamed functions -> Linux mapping:
- kept structs -> Linux mapping:
- new structs -> Linux mapping:
- renamed structs -> Linux mapping:
- kept globals/statics -> Linux mapping:
- new globals/statics -> Linux mapping:
- renamed globals/statics -> Linux mapping:
- kept files -> Linux mapping:
- new files -> Linux mapping:
- renamed files -> Linux mapping:
- kept directories -> Linux mapping:
- new directories -> Linux mapping:
- renamed directories -> Linux mapping:
- items marked NO_DIRECT_LINUX_MATCH:

Linux mapping:
- concept/symbol/path: ...

Consistency:
- Naming: OK/DIFF -> ...
- Semantics: OK/DIFF -> ...
- Flow/Lifetime: OK/DIFF -> ...
- Anti-pattern verdict: NONE/FOUND -> ...

If DIFF:
- Why:
- Impact:
- Plan:
```
