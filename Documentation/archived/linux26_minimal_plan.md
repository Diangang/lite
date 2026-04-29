# Lite OS: The Road to Linux 2.6 Minimal Core


## 文档定位
- 这是一份**Linux 2.6 最小核心演进计划**，属于规划草案。
- 它用于描述目标收敛方向，**不等同于当前代码已经具备的能力**。
- 当前实际落地状态请以 `Linux26-Subsystem-Alignment.md` 和源码为准。

这个文档记录了 Lite OS 向着真正的宏内核（以 Linux 2.6 架构为蓝本）演进的计划和当前状态。

## 当前架构演进状态

### 1. 内存管理 (MM)
- **page_alloc**: 物理页位图分配器
- **paging**: 页表映射，Identity Mapping
- **slab**: 内核堆分配器 (`kmalloc` / `kfree`)
- **Page Cache**: 已经实现基础的页缓存结构 (`address_space`, `page_cache_entry`)，支持按页进行文件数据的读写。

### 2. 进程管理 (Task)
- 简单的内核线程和用户进程隔离。
- 支持 `fork`, `execve`, `exit`, `waitpid` 语义。
- 引入了基于 Dcache 的当前工作目录隔离，即 `struct task_struct` 中维护 `struct vfs_dentry *cwd` 和 `root`，取代了老旧的字符串路径记录。

### 3. 虚拟文件系统 (VFS)
目前 VFS 已经经历了彻底的现代化重构：
- **真正的 Dcache 树**: 废弃了各文件系统自己维护 `children` 链表的做法。所有的目录树拓扑现在统一由全局 Dcache (`struct vfs_dentry`) 管理。
- **Generic VFS Operations**:
  - `generic_readdir`: 统一遍历 `dentry->children`，自动处理 `.` 和 `..`。
  - `generic_file_read/write`: 通过 Page Cache 完成内存页粒度的文件读写。
- **Mount Namespace (简化版)**:
  - 采用了 dentry 覆盖的机制，`path_walk` 支持挂载点跳跃。
  - 消除了启动引导时的 `vfs_boot_cwd`，采用 Linux 风格的根目录初始化。
- **文件系统**:
  - `ramfs`: 退化为极简的数据壳，完全依赖 VFS 的 Generic 方法运作。
  - `initrd`, `procfs`, `devtmpfs`, `sysfs`: 全面支持 VFS 新架构。
  - `minixfs`: 实现了基本的 Minix 文件系统读写支持，包括文件创建、删除、读写等操作。

### 4. 设备驱动模型 (Drivers)
- `kobject` / `kset` (简化版)：在 `sysfs` 中暴露内核对象。
- 终端子系统 (TTY)：简单的输入环形缓冲区和回显机制。
- 串口和 VGA：基于中断（Serial）和内存映射（VGA）的控制台。
- **PCI/PCIe 子系统**：实现了基本的 PCI 设备枚举和配置空间访问。
- **NVMe 驱动**：实现了 NVMe 控制器初始化、命名空间管理和块设备注册，支持 NVMe 设备检测和访问。

## 下一步演进计划 (Roadmap)

### 1. 块设备与存储栈 (Block Layer)
- 引入 `struct block_device` 和 `struct gendisk`。
- 实现简易的通用块层和 I/O 调度队列 (`request_queue`, `bio`)。
- 开发一个简单的 ATA/IDE 硬盘驱动。
- **Page Cache Writeback**: 实现脏页标记和回写机制，让 Page Cache 与块设备真正联动。

### 2. 真实磁盘文件系统 (Ext2)
- 在有了块设备之后，引入 Ext2 文件系统的解析。
- `super_block`、`inode` 与磁盘上物理数据结构的映射。
- 完善 VFS 层的 `lookup`, `create`, `unlink`。

### 3. 用户空间与系统调用完善
- 增加更多的系统调用（如 `mmap` 的写回支持，`stat`, `lseek` 等）。
- 实现 `chroot` 和更完整的 Mount Namespace。
- 引入更健壮的信号（Signal）机制。
