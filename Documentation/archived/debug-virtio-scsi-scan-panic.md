# [CLOSED] virtio-scsi-scan-panic

## Symptom
- 引入 virtio-scsi 多盘持有与总扫描预算后，`make smoke-512` 在启动早期触发 `HALT: KERNEL PANIC: Null pointer access.`
- panic 出现在 `Serial driver interrupts enabled.` 之后、`Syscall handler installed.` 之前。

## Repro
- `make smoke-512`

## Hypotheses
1. `virtscsi_scan()` 在继续枚举 absent target/lun 时，触发了 `scsi_add_device()` / `device_unregister()` 的生命周期 bug。
2. `virtscsi_store_disk()` 或多盘数组清理路径写坏了 HBA 私有状态，导致 remove/unwind 时解引用空指针。
3. 总扫描预算逻辑让 probe 期对过多 target/lun 发命令，命中了当前 Lite SCSI/driver-core 未覆盖的异常路径。
4. panic 并不在成功发现磁盘后，而是在“失败 target/lun 的回收”路径上发生，和真实 I/O 路径无关。

## Plan
- 第一步只加最小运行时插桩，覆盖 `virtscsi_scan()`、`virtscsi_try_scan_target()`、失败回收与 remove/unwind 路径。
- 用日志确认 panic 前最后一个成功/失败的扫描点，再决定是否收缩扫描流程或修正对象生命周期。

## Evidence
- pre-fix:
  - `virtscsi: try ch=0 id=0 lun=10`
  - 随后在 `scsi_add_device()` 期间触发 `HALT: KERNEL PANIC: Null pointer access.`
  - 更早日志显示：在对 absent LUN 做扫描时，代码会反复 `scsi_add_device()` 再立即 `device_unregister()`
- analysis:
  - 假设 1 confirmed：panic 落在“失败 LUN 的注册/回收 churn”路径，而不是正常 I/O 路径
  - 假设 2 rejected：多盘数组本身不是直接崩溃点，`store_disk` 只在已发现 disk 上执行
  - 假设 3 confirmed：总扫描预算把更多 absent LUN 暴露出来，触发了 Lite 当前 driver-core/SCSI 临时对象路径的问题
  - 假设 4 confirmed：根因是“先注册 `scsi_device`，再做 TUR/INQUIRY”的顺序不对

## Fix
- 将 `virtscsi_try_scan_target()` 调整为：
  - 先 `scsi_alloc_device()`
  - 直接做 `TEST UNIT READY` / `INQUIRY` / `READ CAPACITY`
  - 只有确认是可用 disk 后，才 `scsi_add_device()` 并继续 `scsi_add_disk()`
- 对失败的 LUN，不再走 `device_unregister()`，只释放尚未注册的 `sdev` 内存
- 同时把 SCSI host/device/disk 的名字缓冲区落到对象内存里，避免后续多对象场景再依赖临时栈缓冲

## Post-fix
- `virtscsi: try ch=0 id=0 lun=10`
- `virtscsi: alloc sdev=...`
- `virtscsi: tur ch=0 id=0 lun=10 ret=-1`
- `virtscsi: free after tur miss sdev=...`
- 后续启动继续完成：
  - `Syscall handler installed.`
  - `kernel_init: exec /sbin/init as PID 1`
- smoke:
  - `Virtio-SCSI Device -- virtio-scsi disk detected OK.`
  - `Virtio-SCSI Raw R/W -- virtio-scsi raw rw OK.`

## Follow-up optimization
- 在确认 panic 根因后，进一步把扫描逻辑从 `virtio_scsi.c` 下沉到 `scsi.c` 的最小枚举接口：
  - 新增 `scsi_scan_host_selected(shost, channel, id, lun)`
  - `virtio_scsi.c` 不再维护启发式早停，而是显式调用 `scsi_scan_host_selected(shost, 0, SCSI_SCAN_WILD_CARD, SCSI_SCAN_WILD_CARD_LUN)`
  - 当 `lun` 是 wild-card 时，`scsi.c` 优先走最小 `REPORT LUNS`；失败后回退到 `lun 0`
- 结果：
- 预优化时驱动里需要预算和空 target/LUN 早停启发式
- 现在驱动只保留显式边界接口，启动日志收敛为 `selected-lun=wildcard`
- 运行时证据：
  - `scsi: report luns ch=0 id=0 ret=0 alloc_len=131080`
  - `scsi: report luns ch=0 id=0 count=1`
  - `smoke-512` 仍然通过

## Cleanup
- 已移除 `drivers/scsi/scsi.c` 与 `drivers/scsi/virtio_scsi.c` 里的临时调试插桩。
- 在 cleanup 后，`virtio-scsi` 的扫描边界继续收敛为显式 `channel 0 / target 0 / wildcard lun`。
