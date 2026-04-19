# Documentation Guide

## 文档定位
- 这份索引用于说明 `Documentation/` 下每份活跃文档的用途。
- 目标是把“当前实现说明”、“对齐进度”、“规划草案”、“历史问题日志”区分开，避免重复和误读。

## 权威文档

### 当前实现行为
- [QA.md](file:///data25/lidg/lite/Documentation/QA.md)
- 用途：回答“系统现在怎么工作”，优先对应当前代码与运行行为。

### Linux 2.6 对齐进度
- [Linux26-Subsystem-Alignment.md](file:///data25/lidg/lite/Documentation/Linux26-Subsystem-Alignment.md)
- 用途：跟踪仍在推进的子系统、阶段状态和显式 DIFF。

### 设备驱动模型
- [device_driver_model.md](file:///data25/lidg/lite/Documentation/device_driver_model.md)
- 用途：解释 `device / driver / bus / class / kobject / sysfs / devtmpfs` 在当前代码中的语义。

### 内存布局
- [memory_layout.md](file:///data25/lidg/lite/Documentation/memory_layout.md)
- 用途：说明当前物理内存、内核虚拟地址、用户虚拟地址和关键映射关系。

### 代码树目录地图
- [Directory-Structure.md](file:///data25/lidg/lite/Documentation/Directory-Structure.md)
- 用途：说明顶层目录、模块边界和主要文件归属。

### 源码导读
- [Annotation.md](file:///data25/lidg/lite/Documentation/Annotation.md)
- 用途：站在主链路视角帮助建立整体结构，不替代源码本身。

## 现状 + 规划混合文档

### Console / TTY / Serial
- [Console-TTY-Serial-Alignment.md](file:///data25/lidg/lite/Documentation/Console-TTY-Serial-Alignment.md)
- 用途：前半部分用于描述当前 console/tty/serial 现状，后半部分用于记录后续对齐方案。

## 规划 / 审计 / 参考文档

### 长期路线图
- [ArchitectureRoadmap.md](file:///data25/lidg/lite/Documentation/ArchitectureRoadmap.md)
- [roadmap.md](file:///data25/lidg/lite/Documentation/roadmap.md)
- [linux26_minimal_plan.md](file:///data25/lidg/lite/Documentation/linux26_minimal_plan.md)
- 用途：记录候选方向和中长期计划，不代表当前代码已经具备这些能力。

### 专项路线图
- [device_model_roadmap.md](file:///data25/lidg/lite/Documentation/device_model_roadmap.md)
- 用途：记录 driver core / sysfs / devtmpfs 后续收敛方向。

### 审计 / 矩阵
- [linux26_struct_audit.md](file:///data25/lidg/lite/Documentation/linux26_struct_audit.md)
- [linux26_alignment_matrix.md](file:///data25/lidg/lite/Documentation/linux26_alignment_matrix.md)
- 用途：做命名、路径和目录映射审计，不直接代表运行时行为已经完全一致。

## 历史问题日志
- [Issues.md](file:///data25/lidg/lite/Documentation/Issues.md)
- 用途：保留历史故障、定位过程和修复路径，适合排障复盘，不适合作为当前行为说明。
