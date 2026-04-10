# Lite 设备模型与 Linux 2.6 基础架构差异与完善路线

本文聚焦“驱动核心（driver core）/ 设备模型（device model）/ sysfs/devtmpfs 呈现”的基础架构对齐，不讨论具体驱动功能与性能统计细节。

## 1. Lite 当前设备模型的骨架（现状）

- **对象**：`struct device` / `struct device_driver` / `struct bus_type` / `struct class`
- **组织**：
  - `devices/drivers/classes` 三个 kset（用于 sysfs 视图）
  - `bus->devices` 与 `bus->drivers` 两个链表（用于 match/probe）
  - `class->devices` 链表（用于 `/sys/class/<name>`）
- **绑定**：
  - `device_add()` 会把 device 纳入 kset + bus/class 列表，并触发 `device_attach()`
  - `driver_register()` 会把 driver 纳入 kset + bus 列表，并触发 `driver_attach()`
- **sysfs**：
  - `/sys/devices`：显示没有 parent 的顶层 device；device 目录内枚举 `type/bus/driver/parent/children`
  - `/sys/bus`：按 bus 注册链表动态枚举，并为每条 bus 提供 `devices/` 与 `drivers/`
  - `/sys/class`：当前依赖 class kset，提供 `tty/block/console` 等 class 视图
- **devtmpfs**：对少量名字与 `type=="block"` 做规则映射，创建 `/dev/*` 节点
- **uevent**：只做内核侧缓冲与 `/sys/kernel/uevent` 可观测，不实现 udev/hotplug 动作

## 2. 与 Linux 2.6 driver core 的关键差异（基础架构层）

### 2.1 sysfs 三张表的“链接语义”不同

Linux 2.6 的基本原则是：
- `/sys/devices` 是“真实设备对象树”（每个 device 只有一个真实目录位置）
- `/sys/bus/<bus>/devices/*` 与 `/sys/class/<class>/*` 通常是 **指向 `/sys/devices` 真实目录的符号链接**

Lite 当前做法更像：
- 历史上各视图直接复用 device 目录入口（更接近 hardlink/同 inode 多入口）

当前进展：
- 已补上 VFS 的最小 symlink 跟随，并把 `/sys/bus/*/devices/*`、`/sys/class/*/*` 做成指向 `/sys/devices/...` 的 symlink
- 因此“真实路径唯一”的语义开始成立，上层工具也更接近 Linux 预期

影响：
- 后续如果要引入更多属性目录或层次，symlink 语义能显著降低重复与歧义

### 2.2 bus/class 的 sysfs 呈现是“硬编码”，缺少可扩展的注册视图

Linux：
- `bus_register()`/`class_register()` 会自然把 bus/class 的 kobject 挂到 sysfs 的根层次（`/sys/bus`、`/sys/class`），并能动态枚举

Lite：
- `/sys/bus` 已经完成动态枚举（不再写死 `platform/pci`），但 bus 自身还没有进入一个类似 Linux `bus_kset` 的统一管理对象
- `/sys/class` 的入口已经是 kset 枚举，但 class 目录结构仍不完整（如 `class/<name>/devices` 链接集、以及更接近 Linux 的 class/driver 组织）

影响：
- 引入更多总线（i2c/spi/scsi/virtio/usb）时，sysfs 结构需要手动改代码才能出现

### 2.3 device/driver 生命周期与并发模型极简

Linux 2.6：
- 设备注册与驱动绑定存在更完整的状态机（add/del、attach/detach、probe/remove）
- 有 deferred probe（EPROBE_DEFER）、异步 probe、以及更细的锁与引用计数

Lite：
- `device_add()`/`driver_register()` 都是同步探测模型（`device_attach()`/`driver_attach()`）
- 设备/驱动列表缺少并发保护、缺少 deferred probe 的“依赖驱动顺序”表达

影响：
- 当设备之间存在依赖（例如控制器先起来、子设备才能枚举）时，缺少统一表达方式

### 2.4 `devtmpfs` 与 “dev_t(major:minor)” 的统一通道缺失

Linux 2.6：
- `struct device` 通常有 devt（major/minor），devtmpfs/udev 以 devt 为核心做节点管理
- char/block 设备分别有 cdev/bdev/gendisk 的体系与注册路径

Lite：
- major/minor 已经统一进 `struct device`（并且 `tty_device/gendisk` 继续保留子系统内部语义）
- devtmpfs 已改为优先看 `devnode_name + (major,minor)` 创建节点，避免基于名字的硬编码分支增长

影响：
- 一旦要扩展更多字符设备/块设备类型，规则映射不可维护，且难以对齐 Linux 的用户态习惯

### 2.5 缺少 Linux 常用的“辅助基础设施”挂点

Linux 2.6 driver core 常见基础能力（Lite 目前缺）：
- `modalias`（sysfs）与基于 modalias 的驱动自动加载逻辑（即使没有 udev，也应能生成）
- `driver_override`（手动选择驱动）
- `uevent` 环境变量生成（ACTION/DEVPATH/SUBSYSTEM/MODALIAS）
- `device_type`（同一类设备共享行为与 attribute）
- `attribute groups`（sysfs 属性组织与复用）
- `device links`（非 parent-child 的依赖关系表达，如 supplier/consumer）

## 3. 建议的“下一步基础架构完善路线”（按优先级）

### 3.1 让 sysfs 的 bus/class 视图变成“动态枚举”

目标：
- `/sys/bus` 已完成遍历 bus 注册链表动态枚举
- `/sys/class` 进一步补齐为 Linux 风格目录结构（至少具备 `devices/` 链接集）

收益：
- 后续引入新 bus/class 不需要改 sysfs 的硬编码表
- 更接近 Linux 的“注册即出现”行为

现阶段建议：
- 先把 `/sys/class/<name>` 从“直接列设备目录”升级为：
  - `/sys/class/<name>/` 下提供 `devices/`（链接集）
  - 保留兼容入口（如果需要）再逐步迁移

### 3.2 引入 “链接语义”：用 symlink（或等价机制）表达 bus/class 视图

目标（已完成第一阶段）：
- `/sys/bus/<bus>/devices/*` 与 `/sys/class/<class>/*` 指向 `/sys/devices` 的链接对象

实现建议（按成本从低到高）：
- 已采用：最小 symlink（read + path_walk 跟随），只支持绝对目标（满足 sysfs 需求）

收益：
- 与 Linux 用户态工具习惯一致
- “设备真实位置唯一”，减少后续属性层次引入时的混乱

### 3.3 把 devtmpfs 从“名字规则”升级为“devt 驱动”

目标（已完成第一阶段）：
- 在 `struct device` 统一引入 `devt(major/minor)` 与 `devnode_name`
- devtmpfs 根据 `devnode_name + devt` 选择节点创建路径（当前仍只支持 tty/console 这类最小字符设备分发）

依赖拆分建议：
- 第一步：设备模型层统一 devt（不引入完整 cdev/bdev 体系也可以）
- 第二步：块设备走 `gendisk`，字符设备走最小 cdev，devtmpfs 统一入口

收益：
- 扩展更多设备节点几乎不需要改 devtmpfs
- 更接近 Linux 的基础抽象与分层

### 3.4 最小化引入 “modalias + uevent env”

目标（已完成第一阶段）：
- sysfs 为每个 device 增加 `modalias`
- uevent 缓冲输出扩展为 Linux 风格 env：`ACTION/DEVPATH/SUBSYSTEM/MODALIAS/DEVNAME/MAJOR/MINOR`

收益：
- 即使没有 udev，也能形成 Linux 风格的“事件描述”
- 将来接用户态守护/脚本才有稳定接口

### 3.5 引入最小 deferred probe / 依赖表达

目标（已完成第一阶段）：
- `probe()` 支持返回 `-EPROBE_DEFER`
- driver core 维护最小 deferred 列表，并在 `driver_register()` 后触发重试

收益：
- 存储/总线类驱动通常存在初始化依赖，deferred probe 能显著降低“初始化顺序耦合”

## 4. 建议的验收标准（不涉及性能统计）

- 新增 bus/class 不改 sysfs 代码即可出现在 `/sys/bus`、`/sys/class`
- 同一 device 在 `/sys/devices` 只有一个真实目录；bus/class 视图以链接方式指向它
- devtmpfs 不依赖设备名字硬编码，新增设备节点不需要改 devtmpfs 代码
- uevent 输出包含最小 Linux 风格字段（ACTION/DEVPATH/SUBSYSTEM/MODALIAS）
