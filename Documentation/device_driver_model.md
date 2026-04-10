# 设备驱动模型：device / driver / bus / class（Lite 与 Linux 2.6 语义对齐）

本文把项目里零散的问答与实现现状汇总成一份“设备驱动模型”文档，并补充对 PCIe/NVMe 的分析说明，帮助用统一视角理解 platform、I2C/SPI、UFS 与 PCIe/NVMe 的差异与联系。

## 1. 统一视角：Linux 风格 driver model 的最小对象集

可以把 Linux 2.6（以及本项目 Lite）的 driver model 拆成五个概念层：

- `kobject`：可命名、可组织成层级、可参与引用计数的最小对象底座（sysfs 的语义核心）。
- `device`：具体“设备实例”，代表一个可被驱动管理的对象（可能对应真实硬件，也可能是逻辑设备）。
- `driver`：可与 device 匹配并 `probe()` 的驱动实体。
- `bus`：设备与驱动的组织域，提供 `match(dev, drv)` 决定谁能绑定谁。
- `class`：面向用户语义的分组（例如 tty、block、net），主要用于 `/sys/class` 与 `/dev` 视图组织。

在 Lite 中这些对象关系与可见化已经具备最小框架（参考 [Annotation.md](file:///data25/lidg/lite/Documentation/Annotation.md#L426-L486)、[QA.md](file:///data25/lidg/lite/Documentation/QA.md#L352-L409)）。

## 2. Lite 当前实现：它是“Linux 2.6 风格骨架”，但仍是极简版

### 2.1 driver core 初始化入口

- 初始化入口：`driver_init()`（由 `start_kernel()->do_basic_setup()` 触发）。
- 建立并初始化：
  - `platform` bus
  - `console` class
  - `tty` class
  - `devices/drivers/classes` 三组 kset

对应实现：[init.c](file:///data25/lidg/lite/drivers/base/init.c#L33-L59)。

### 2.2 设备注册、驱动注册、绑定（bind）

Lite 的关键动作是“注册（register）”与“绑定（bind/probe）”：

- `device_add()`：把 `device` 挂入 kset/bus/class 视图，创建 devtmpfs 节点（如有），并触发 attach。
- `device_attach()`：遍历同一 bus 上已注册的 driver，用 `bus->match()` 做匹配，匹配成功则调用 `drv->probe(dev)` 并记录绑定关系。

对应实现：
- [core.c](file:///data25/lidg/lite/drivers/base/core.c#L96-L158)

要点：
- “创建一个 device 结构体”不是重点；重点是把它纳入 bus/class 组织并完成匹配绑定。
- Lite 当前的绑定是“同步尝试绑定”，没有 Linux 那样复杂的并发/延迟探测模型。

### 2.3 uevent：Lite 只有“最小事件流”，没有用户态守护进程消费

Lite 内核侧会追加文本事件到一个缓冲区，并通过 sysfs 暴露给用户态读取：
- `/sys/kernel/uevent`（只做可观测，不做 udev/hotplug 自动动作）

当前 uevent 的基本格式已对齐到 Linux 风格 env（多行 `KEY=VALUE`，事件之间用空行分隔），最小包含：
- `ACTION`
- `DEVPATH`
- `SUBSYSTEM`
- `MODALIAS`
- `DEVNAME` / `MAJOR` / `MINOR`（若该 device 有 devt）

实现参考：
- [core.c](file:///data25/lidg/lite/drivers/base/core.c#L338-L386)
- [sysfs.c](file:///data25/lidg/lite/fs/sysfs/sysfs.c#L89-L94)

## 3. platform bus：逻辑总线，不等同于“物理电路总线”

platform bus 的定位：
- 它不是 PCIe/USB 那种可枚举的物理协议总线。
- 它是“板级固定设备/不可枚举设备”的逻辑容器，把这些设备纳入统一 driver model（device/driver/bus/match/probe）。

在 Lite 中，x86 平台现在只静态注册最小“硬件存在声明”：
- `serial0`（平台串口设备）

而更高层的用户语义设备由各自子系统创建：
- `console`：由 console 子系统注册为 `console` class 设备
- `tty`：由 tty 子系统注册为 `tty` class 设备
- `ttyS0`：由 serial/tty 子系统通过最小 `tty_driver` 路径注册为 `tty` class 设备，并挂在 `serial0` 之下

见 [setup.c](file:///data25/lidg/lite/arch/x86/kernel/setup.c#L13-L28)。

## 4. “控制器 device”与“控制器下面的设备 device”：一条硬件链路对应多层 device

容易混淆的一点是：Linux 中“一个物理硬件路径”往往对应多层 `struct device`：
- 控制器本体（host/controller）也有一个 device。
- 控制器驱动起来后，可能注册一个“子总线/子系统”，并在其上创建多个 device。
- 存储类设备还会进一步进入 SCSI/block 层，派生出新的 device。

可以用一句话记忆：
- platform/PCI 这类更多描述“控制器从哪来、资源怎么拿”；
- I2C/SPI/SCSI/block 这类更多描述“控制器下面挂了什么设备、设备怎么呈现给上层”。

## 5. Linux 设备模型的三张表：`/sys/devices`、`/sys/bus`、`/sys/class`

理解 Linux driver model，最容易混淆的是“根在哪里、bus 在哪一层、device 又在哪一层”。建议把 sysfs 视角拆成三张表看：

### 5.1 `/sys/devices`：实际设备对象树

`/sys/devices` 更接近“真实设备对象树”：
- 这里按 parent-child 关系组织实际 `device`
- 一个控制器下面再派生出来的设备，往往也会继续挂在这棵树上
- 所以它是“设备对象层级”，不是“总线类型目录”

可以把它理解成 Linux driver model 的“设备根视图”。

### 5.2 `/sys/bus`：总线类型总表

`/sys/bus` 更接近“当前系统已经注册了哪些 `bus_type`”：
- 每个子目录通常对应一个 bus 类型，如 `platform`、`pci`、`usb`、`i2c`、`spi`、`scsi`
- 每个 bus 下面又会有：
  - `devices/`
  - `drivers/`

所以：
- `/sys/bus` 不是“设备树”
- 它是“总线类型索引”
 - 在 Linux 中 `/sys/bus/<bus>/devices/*` 通常是指向 `/sys/devices` 的 symlink；Lite 目前也采用 symlink 语义把 bus 视图指向真实 device 目录。

### 5.3 `/sys/class`：面向用户语义的设备分组

`/sys/class` 更接近“用户如何理解这类设备”：
- 如 `tty`、`block`、`net`
- 这里强调的是“设备用途/语义”，不是“它通过哪条总线发现”

例如：
- 一个 NVMe namespace 最终会在 `/sys/class/block` 或 `/sys/block` 里呈现为块设备
- 但它的控制器来源仍然是 PCI bus
 - 在 Linux 中 `/sys/class/<class>/*` 通常也是指向 `/sys/devices` 的 symlink；Lite 同样把 class 视图指向真实 device 目录。

### 5.4 一句话区分三者

- `/sys/devices`：设备对象树
- `/sys/bus`：总线类型表
- `/sys/class`：用户语义分组

## 6. Linux 里有没有“一个根节点”

要区分两种“根”：

### 6.1 设备树（Device Tree, DT）的根

如果说的是 ARM/嵌入式常见的 `.dts/.dtb`：
- 它有一个明确的根节点 `/`
- 下面是 `cpus`、`memory`、`soc`、`i2c@...`、`serial@...` 等节点

这是“硬件描述树”的根。

### 6.2 Linux driver model 的根

如果说的是 Linux 运行时的设备模型：
- 更接近 `/sys/devices` 这棵设备对象树
- 它同样有“根层级”的概念，只是不是 DT 里的 `/`

所以不能把 DT 根节点和 sysfs 设备对象根混为一谈：
- DT 根是“固件/硬件描述的根”
- `/sys/devices` 是“内核运行时设备对象的根视图”

## 7. Linux 里到底有多少 `bus_type`

这个数量没有固定答案，要分“源码里定义了多少”和“运行时注册了多少”两层看。

### 7.1 源码视角

在 Linux 源码里，很多子系统都会定义自己的 `struct bus_type` 全局变量，例如：
- `platform_bus_type`
- `pci_bus_type`
- `usb_bus_type`
- `scsi_bus_type`
- `i2c_bus_type`

也就是说，Linux 不是“只有几条总线”的简单模型，而是“很多 bus_type 并存，每条 bus 有自己的匹配、设备列表和驱动列表”。

### 7.2 运行时视角

真正出现在系统里的 bus 数量取决于：
- 内核配置
- 当前架构
- 驱动是否编进内核或被加载
- 硬件是否存在并完成初始化

所以运行时要看：
- `/sys/bus` 下实际出现了哪些目录

结论：
- bus_type 数量不是固定常量
- “一级 bus 有多少个”本质上是一个运行时问题，而不是只看源码就能定死

## 8. I2C / SPI：控制器常是 platform device，但从设备挂在专用 bus 上

以 I2C 为例（SPI 类似）：

- 第 1 层：I2C 控制器本体（adapter/controller）
  - SoC 内的 I2C 控制器通常被描述为 platform device（由 DT/ACPI/板级代码声明）。
  - 该 device 挂在 platform bus（或在 PC 平台上可能是 PCI）。
- 第 2 层：I2C 子系统总线
  - 控制器驱动 `probe()` 成功后注册 `i2c_adapter`，形成一条 I2C bus。
- 第 3 层：I2C 从设备
  - 每个从设备在内核中会以 `i2c_client`（内嵌 `struct device`）出现，挂在 I2C bus 上。

关键点：
- I2C/SPI 多数情况下不可“自枚举”，设备信息通常来自 DT/ACPI/板级描述。
- 所以“控制器 device”来自 platform/PCI，“外设 device”来自 I2C/SPI bus。

## 9. UFS：控制器是 platform device，但盘最终进入 SCSI/block 层级

手机/SoC 上的 UFS 典型呈现：

- 第 1 层：UFS Host Controller（UFSHCI）
  - 通常是 SoC 内 IP，因此常以 platform device 形式出现，挂在 platform bus。
- 第 2 层：UFS driver 把设备纳入存储栈
  - UFS 驱动初始化链路后，会把它抽象成 SCSI host，并由 SCSI mid-layer 管理 LUN/设备。
- 第 3 层：块设备呈现
  - 最终会出现 block 层对象（磁盘/分区/队列），面向用户态呈现为 `/dev/...`。

要点：
- platform 解决“控制器从哪来”；SCSI/block 解决“存储设备怎么被上层使用”。
- 因此“UFS 挂在 platform bus”通常指的是“UFS 控制器”，而不是“整个存储抽象只在 platform 上结束”。

## 10. SCSI 与 block：它们也有 device，但处在更高层

### 10.1 SCSI：协议设备层

SCSI 在 Linux 中不是简单“一个驱动文件”，而是一整套 mid-layer：
- `SCSI host`：控制器抽象
- `scsi_device`：具体 SCSI 设备对象
- 往上再派生出磁盘、磁带、光驱等更具体的设备语义

所以：
- SCSI 这一层有自己的 device
- 它也有自己的组织域与 sysfs 视图
- 但它不是“最底层物理可枚举总线”的同义词

### 10.2 block：最终块设备呈现层

block 层更接近“用户看到的块设备”：
- 典型对象是 `gendisk`
- 对应 `/dev/sda`、`/dev/nvme0n1`、`/dev/mmcblk0`
- sysfs 里通常体现在 `/sys/block` 或 block class 视图

所以 block 也有自己的设备对象语义，但它更像“最终服务对象”，而不是“设备发现来源”。

在 Lite 当前实现里，已经补上了最小 `block` class：
- `ram0/ram1`、`nvme0n1` 都会进入 `/sys/class/block`
- 同时设备目录里提供 `parent`，用于显式追踪上层块设备对应的下层控制器/父设备
  - 例如 `ram0 -> platform-root`
  - `nvme0n1 -> 对应 pci_dev`
- 并且在 block class 之下增加了最小 `gendisk` 语义：
  - `gendisk` 负责“盘名 + block_device + 对应 device”的统一呈现
  - `ram0`、`ram1`、`nvme0n1` 现在都通过 `gendisk -> block class device` 注册

### 10.3 一句话理解 SCSI / block

- SCSI：存储协议设备层
- block：最终块设备呈现层
- platform/PCI：更多是“控制器来源层”

## 11. PCIe / NVMe：可枚举物理总线 + 存储协议栈

### 11.1 PCIe 在物理与模型上的含义

PCIe 是可枚举的物理协议互连：
- CPU/SoC 中有 Root Complex（根复合体）与 Root Port。
- 设备侧有 Endpoint 控制器。
- 链路训练成功后，OS 可通过配置空间枚举设备、分配 BAR、启用 Bus Master 等。

在 Linux driver model 中：
- PCI 总线有专用 `bus_type`（pci_bus_type）。
- 每个 PCIe 设备对应一个 `struct pci_dev`（内嵌 `struct device`），挂在 PCI bus 上。

### 11.2 NVMe 相比 UFS/I2C 的“层次差异”

可以把 NVMe 理解成：
- “跑在 PCIe 互连上的一种存储控制器协议”。

与 UFS/I2C 的对比：
- I2C：通常不可枚举；控制器来自 platform；从设备挂 I2C bus；不进入 block 作为主通路。
- UFS：控制器来自 platform；上层进入 SCSI/block；设备发现通过 UFS 协议枚举 LUN。
- NVMe：设备通过 PCIe 枚举发现（配置空间）；上层直接进入 block（现代内核经 blk-mq；早期也可经 SCSI，但主流是原生 NVMe block）。

### 11.3 Lite 当前 PCI/NVMe 的实现定位

Lite 已具备最小路径：
- PCI 扫描、配置空间读取、sysfs 可见化与 bind/unbind（见 [pci.c](file:///data25/lidg/lite/drivers/pci/pci.c)、[sysfs.c](file:///data25/lidg/lite/fs/sysfs/sysfs.c)）
- NVMe 驱动在 PCI bus 上按 class code 匹配并 probe（见 [nvme.c](file:///data25/lidg/lite/drivers/nvme/nvme.c#L48-L56)）
- 当前 NVMe “testing mode”：不实现完整 admin/io queue 协议收发，直接注册一个测试 namespace 并暴露成块设备节点 `/dev/nvme0n1`（见 [nvme.c](file:///data25/lidg/lite/drivers/nvme/nvme.c#L74-L127)）

这条链路验证的是：
- 可枚举总线（PCI）→ 驱动绑定（NVMe）→ 块设备注册 → devtmpfs `/dev` 节点

## 12. 三条典型层级树

### 12.1 I2C

```text
platform device（I2C controller）
  -> i2c_adapter
    -> i2c_client（具体从设备）
```

### 12.2 UFS

```text
platform device（UFS Host Controller）
  -> SCSI host
    -> scsi_device
      -> gendisk / block device
```

### 12.3 PCIe / NVMe

```text
pci_dev（NVMe controller）
  -> nvme controller
    -> nvme namespace
      -> gendisk / block device
```

### 12.4 从底到顶的设备模型全景图

下面把前面分散的层次统一压成一张总图：

```text
                           +----------------------+
                           |      kobject/kset    |
                           |  命名 / 层级 / 引用  |
                           +----------+-----------+
                                      |
                         +------------+------------+
                         |                         |
                         v                         v
                 +---------------+         +---------------+
                 |     bus       |         |     class     |
                 | platform/pci  |         | tty/block/net |
                 +-------+-------+         +-------+-------+
                         |                         |
                         v                         v
                 +---------------+         +---------------+
                 |    device     |-------> | /sys/class/*  |
                 | 控制器/外设实例 |         | /dev/*        |
                 +-------+-------+         +---------------+
                         |
         +---------------+-------------------------------+
         |                               |               |
         v                               v               v
 +---------------+               +---------------+ +---------------+
 | platform dev  |               |   pci_dev     | | other buses   |
 | I2C/UFS host  |               | NVMe ctrl     | | usb/scsi/...  |
 +-------+-------+               +-------+-------+ +---------------+
         |                               |
         | probe()                       | probe()
         v                               v
 +---------------+               +---------------+
 | i2c_adapter   |               | nvme ctrl     |
 | SCSI host     |               | namespaces    |
 +-------+-------+               +-------+-------+
         |                               |
         v                               v
 +---------------+               +---------------+
 | i2c_client    |               | gendisk       |
 | scsi_device   |               | /dev/nvme0n1  |
 +-------+-------+               +-------+-------+
         |                               |
         +---------------+---------------+
                         |
                         v
                 +---------------+
                 |  /sys/devices |
                 | 实际设备对象树 |
                 +---------------+
```

读这张图时抓住三点：
- `kobject/kset` 是所有可见对象的底座；
- `bus` 解决“谁和谁匹配、设备从哪里来”；
- `class` 与 `gendisk`/`tty` 等更高层对象一起，解决“最终怎样呈现给用户态与 VFS”。

### 12.5 真实硬件视角 vs Linux 抽象视角

把“电路上到底怎么连”和“Linux 里对象怎么挂”放在一起看，通常最清楚：

#### PCIe / NVMe

```text
真实硬件视角

CPU/SoC
  -> PCIe Root Complex / Root Port
    -> PCIe 链路（lane / 差分对 / slot / switch）
      -> NVMe Endpoint controller
        -> NAND / Flash media

Linux 抽象视角

pci_bus_type
  -> pci_dev（NVMe controller）
    -> nvme driver
      -> nvme controller
        -> nvme namespace
          -> gendisk
            -> /sys/block/nvme0n1
            -> /dev/nvme0n1
```

这里最关键的是：
- 真实硬件上，NVMe 设备首先是一个 PCIe endpoint；
- Linux 里，OS 先把它当作 `pci_dev` 枚举出来，再由 NVMe 驱动把它提升成 namespace 与 block device。

#### I2C

```text
真实硬件视角

SoC I2C Controller
  -> SDA / SCL 两根线
    -> EEPROM / 传感器 / PMIC / 触摸芯片

Linux 抽象视角

platform device（I2C controller）
  -> i2c controller driver
    -> i2c_adapter
      -> i2c_bus_type
        -> i2c_client（具体从设备）
```

这里最关键的是：
- 真实硬件上，就是一个主控制器通过 SDA/SCL 与多个从设备通信；
- Linux 里，控制器本体常作为 platform device 出现，而具体从设备则挂在 I2C bus 上。

#### UFS

```text
真实硬件视角

SoC UFS Host Controller
  -> UniPro / M-PHY 链路
    -> UFS Device controller
      -> NAND / Flash media

Linux 抽象视角

platform device（UFS host controller）
  -> ufshcd driver
    -> SCSI host
      -> scsi_device
        -> gendisk
          -> /sys/block/sdX
          -> /dev/sdX
```

这里最关键的是：
- 真实硬件上，UFS 不是 PCIe endpoint，而是 SoC 侧 UFS host 和设备侧 UFS device 通过 UFS 链路通信；
- Linux 里，UFS 控制器通常从 platform bus 进入，但上层存储抽象会落到 SCSI/block。

#### 一句话对比

- PCIe/NVMe：底层先是可枚举总线设备，再进入块设备层
- I2C：底层是主从串行控制总线，控制器和从设备分属不同层
- UFS：底层是专用存储链路，控制器来自 platform，上层落到 SCSI/block

这些树说明：
- 控制器 device 和最终对用户可见的设备对象，往往不在同一层
- 一个物理设备路径，Linux 中通常会被抽象成多层 device

### 12.6 Lite 当前的 tty/serial 与 block 细化层次

```text
platform-root
  -> serial0                  (platform device)
    -> console                (console class device)
    -> ttyS0                  (tty class device, 由最小 tty_driver 注册)
  -> tty                      (tty class device)
  -> ram0 / ram1              (block class device)

pci0000:00 / pci device
  -> nvme controller
    -> nvme0n1                (block class device)
```

这里的关键变化是：
- `ttyS0` 不再直接由平台代码硬编码创建，而是通过 `tty_driver -> tty_register_device()` 路径注册；
- `block` 设备统一进入 `block` class，并通过最小 `gendisk` 语义与 `parent` 链接到下层设备，便于从 `/sys/class/block/*` 反查控制器来源。

### 12.7 Lite 当前最小 `tty_driver` 与 `gendisk` 语义

当前项目还没有完整 Linux 2.6 的 tty core / gendisk 子系统，但已经补了最小骨架：

- **tty_driver**
  - `tty_register_driver()`：注册一条最小 tty 驱动线
  - `tty_register_device()`：在这条 tty 驱动线上创建具体 tty 设备
  - 目前 `ttyS0` 走的就是这条路径
  - sysfs 中提供最小属性：
    - `/sys/class/tty/ttyS0/tty_driver`
    - `/sys/class/tty/ttyS0/index`
    - `/sys/class/tty/ttyS0/dev`
- **gendisk**
  - `gendisk_init()`：初始化最小磁盘对象
  - `block_register_disk()`：把 `gendisk` 呈现为 `block` class 设备
  - 目前 `ram0/ram1/nvme0n1` 都通过它进入 `/sys/class/block` 与 `/dev`
  - sysfs 中提供最小属性：
    - `/sys/class/block/ram0/capacity`
    - `/sys/class/block/ram0/queue`
    - `/sys/class/block/ram0/dev`
    - `/sys/class/block/*/parent`

这样做的意义是：
- tty 设备的“驱动抽象”和“设备实例”开始分层；
- block 设备的“盘对象”和“后端 block_device”开始分层；
- 虽然实现仍然极简，但层级结构已经比直接注册裸 `device` 更接近 Linux 2.6。

## 13. console / tty / serial：最小控制台与终端语义

Lite 当前将 console 抽象成“可注册的 console driver 链表”，并由串口注册为一个 console 后端；tty 负责交互语义与行规程风格的输入输出。

参考实现：
- console 框架：[console.h](file:///data25/lidg/lite/include/linux/console.h)、[console.c](file:///data25/lidg/lite/drivers/video/console/console.c)
- 串口注册 console 后端：[serial.c](file:///data25/lidg/lite/drivers/tty/serial/serial.c#L9-L55)
- tty 输出分发（当前输出到串口）：[tty.c](file:///data25/lidg/lite/drivers/tty/tty.c#L79-L86)

对齐 Linux 2.6 的关键点是：
- 串口既可能是 tty 设备，又可能作为 printk 的 console 后端；
- console 输出尽量走短路径并尽量不破坏 tty 的硬件状态（例如保存/恢复 UART IER）。

## 14. 术语速查：把“挂在哪个 bus”一句话说清

- platform 控制器：device 挂在 `platform` bus（逻辑容器）。
- I2C 从设备：device 挂在 I2C bus（由 i2c_adapter 建立）。
- SPI 从设备：device 挂在 SPI bus（由 spi_master 建立）。
- UFS 控制器：device 挂在 platform bus，但盘最终以 SCSI/block 设备呈现。
- PCIe/NVMe：控制器 device（pci_dev）挂在 PCI bus，namespace 最终以 block device 呈现。
- SCSI 设备：挂在 SCSI 这一层的设备组织域中，表示存储协议设备。
- block 设备：最终呈现在 block/class 视图中，表示可供 VFS/用户态使用的块设备。
- `tty_driver`：tty 子系统中的设备驱动抽象，用来把 `ttyS0` 这类具体 tty 设备注册进 `tty` class，而不是直接由平台层硬编码。
- `gendisk`：块层中的“磁盘对象”抽象，用来把后端 `block_device` 组织成 `/sys/class/block` 与 `/dev` 可见的盘设备。

## 15. 本轮问答结论汇总

把前面几轮问答压缩成最核心的结论：

- platform bus 是逻辑总线，不是 PCIe/USB 那种物理可枚举总线。
- 真实物理总线如 PCIe/USB，通常可以理解为“主机侧控制器 + 真实链路 + 设备侧 endpoint/controller”。
- I2C/SPI 也有真实控制器和线路，但很多情况下不能自动枚举，需要 DT/ACPI/板级信息告诉内核总线上挂了谁。
- “控制器本体的 device” 与 “控制器下面具体设备的 device” 往往不在同一个 bus/层级上。
- UFS 常见于手机/SoC，控制器本体常是 platform device，但盘最终进入 SCSI/block 层。
- NVMe 跑在 PCIe 上，控制器通过 PCI 枚举被发现，namespace 最终以 block device 呈现。
- `/sys/devices`、`/sys/bus`、`/sys/class` 三者分别回答：
  - 设备对象树在哪里
  - bus_type 有哪些
  - 用户语义设备怎么分组
