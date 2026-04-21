---
name: "linux-alignment"
description: "强制 Lite Kernel 与 Linux 在符号、文件落位、语义、流程上对齐。新增或修改内核代码时调用；必须先给 Linux 依据与映射账本，再允许编辑。"
---

# Linux Alignment

本技能用于约束 Lite Kernel 的改动必须以 Linux 为基准完成对齐。检查重点只有四类：
- 符号是否能在 Linux 找到直接对应
- 函数和结构体是否放在与 Linux 相同的文件
- 对外语义是否与 Linux 一致
- 流程与生命周期是否与 Linux 一致

允许“少实现”的简化，不允许用 Lite 自创模型替代 Linux 已有模型。

## 何时必须调用

满足任意一条即必须调用：

1. 新增、删除、重命名、迁移任何函数、结构体、文件、目录。
2. 修改任何内核对外行为，例如 sysfs、procfs、uevent、ioctl、设备命名、路径、用户态 ABI。
3. 修改关键流程或生命周期，例如 init/probe/remove、注册/发布/解绑、父子关系、引用计数、对象释放。
4. 任何你准备声称“这是 Linux 对齐”的代码修改。

## 核心原则

### 1. Reference-First

先读 Linux，再读 Lite，再决定要不要改。

禁止：
- 先改 Lite，后找 Linux 依据
- 只凭术语印象声称“Linux 也是这样”
- 用“更优雅”“更统一”替代 Linux 代码依据

### 2. Same Symbol, Same File

对于函数和结构体，必须同时满足：
- Linux 有对应符号
- Lite 名称与语义对齐
- Lite 落位文件与 Linux 对应文件一致

硬规则：
- 若 Linux 对应项位于 `linux2.6/<path>/<file>`，Lite 也必须位于对应 `<path>/<file>`
- Lite 当前没有该文件，就按 Linux 路径新增文件
- 禁止把 Linux 在 `A.c/A.h` 的函数或结构体放到 Lite 的 `B.c/B.h`
- 禁止把“先放别处，后面再挪”作为默认做法

一句话：**同名、同义、同文件。**

### 3. Linux-First, Not Idea-First

如果 Linux 已有明确实现、命名、文件落位、流程：
- 必须直接对齐 Linux
- 不得先设计 Lite 自己的一层中间抽象

如果 Linux 没有直接对应：
- 必须标记 `NO_DIRECT_LINUX_MATCH`
- 必须写 `Why/Impact/Plan`
- 必要时先询问用户

### 4. Simplify by Subsetting, Not Replacing

允许简化，只能是“少实现 Linux 的一部分”：
- 可以暂时不实现完整能力
- 不可以发明 Linux 没有的一层模型来替代

## 编辑前硬闸门

### Gate 0. Ledger First

任何编辑前，必须先提交可复核账本。

账本至少包含：
- `Files`
- `functions`
- `structs`
- `globals/statics`
- `directories`

对每个函数和结构体，必须额外给出：
- `Linux symbol`: `linux2.6/<path>/<file>::<symbol>`
- `Lite file`
- `Placement`: `OK` 或 `DIFF`

若 `Placement=DIFF`：
- 默认禁止继续做语义修改
- 必须先收敛落位，或向用户说明为什么本轮不能收敛

### Gate 1. 三步证据

修改前必须先完成：
1. 读取 Lite 当前实现
2. 读取 Linux 对应实现
3. 明确本轮只改哪一个差异点

未完成这三步，禁止编辑。

### Gate 2. 一次只收敛一个差异点

每轮只允许围绕一个明确差异推进，例如：
- 文件落位对齐
- 命名对齐
- 返回码或错误语义对齐
- 生命周期时机对齐

禁止一轮中混入“顺手优化”“顺手抽象”“顺手统一”。

### Gate 3. 无 Linux 对应则停止扩展

若 Linux 中没有直接对应：
- 默认不新增
- 必须标记 `NO_DIRECT_LINUX_MATCH`
- 必须写 `Why/Impact/Plan`

## 必做检查

### A. Placement

对每个涉及函数和结构体，必须检查：
- Linux 文件是什么
- Lite 文件是什么
- 是否同文件

判定规则：
- 同文件：`Placement: OK`
- 不同文件：`Placement: DIFF`

`Placement: DIFF` 不能因为“语义相同”而视为通过。

### B. Naming

对每个涉及对象，必须检查：
- 名称是否为 Linux 术语
- 是否引入 Linux 中不存在的新术语

### C. Semantics

对每个对外行为，必须检查：
- 路径是否一致
- 字段名是否一致
- 返回码是否一致
- 用户态可观察结果是否一致

### D. Flow/Lifetime

必须检查以下反模式：
- init/probe/remove 职责混杂
- core 注册与实例发布混杂
- 按名字回捞对象再绑定
- 先假设对象存在，再事后绑定
- 派生对象脱离 owner 生命周期

命中任意一项，都必须记为 `Flow/Lifetime: DIFF`。

## 明确禁止

禁止以下行为：

1. 先改后找 Linux 依据。
2. 只对齐命名，不对齐文件落位。
3. 只对齐语义，不对齐文件落位。
4. 因为“当前文件更方便”把函数或结构体放到 Linux 对应文件之外。
5. Lite 缺文件时，不建对应文件，而把对象塞进现有文件。
6. 用 Lite 自创 helper、bridge、manager、wrapper 替代 Linux 已有模型。
7. 修改现有代码时只看主流程，不核查文件、目录、结构体、变量。
8. 在未完成账本和 Linux 证据前开始编辑。

## 允许的例外

只有同时满足以下条件，才允许暂时 `Placement: DIFF`：
- Lite 当前缺少 Linux 对应的整层基础设施
- 当前差异无法在本轮最小修改内消除
- 已明确写出 `Why/Impact/Plan`
- 用户接受该差异

否则默认不允许。

## 推荐执行顺序

1. 找 Linux 对应文件与符号
2. 找 Lite 当前文件与符号
3. 先判断 `Placement`
4. 若 `Placement: DIFF`，优先处理落位
5. 再处理语义和流程差异
6. `make clean`
7. `make -j4`
8. `make smoke-512`

## 输出模板

```text
Linux Alignment Report

Change scope:
- files:
- directories:
- public surface:

Reference-first evidence:
- Linux file:
- Linux symbol:
- Lite file:
- This step only changes:

Mapping ledger:
- functions:
  - <symbol>: linux2.6/<path>/<file>::<symbol>, lite=<file>, placement=OK/DIFF
- structs:
  - <symbol>: linux2.6/<path>/<file>::<symbol>, lite=<file>, placement=OK/DIFF
- globals/statics:
- files:
- directories:
- NO_DIRECT_LINUX_MATCH:

Consistency:
- Naming: OK/DIFF -> ...
- Placement: OK/DIFF -> ...
- Semantics: OK/DIFF -> ...
- Flow/Lifetime: OK/DIFF -> ...

If DIFF:
- Why:
- Impact:
- Plan:
```
