# [CLOSED] virtio-scsi-missing

## Symptom
- QEMU 已加入 `virtio-scsi-pci` 和 `scsi-hd` 后端磁盘。
- `make smoke-512` 通过，但 `smoke` 里 `/dev/sda` 不存在，`virtio-scsi` 两个测试被跳过。

## Repro
- `make smoke-512`

## Hypotheses
1. PCI 未命中 `virtio-scsi-pci` 的 vendor/device id，`virtscsi_probe()` 没有执行。
2. `virtscsi_probe()` 执行了，但在 legacy virtio PCI queue/config 初始化阶段失败返回。
3. virtio queue 初始化完成，但 `scsi_test_unit_ready` / `scsi_inquiry` / `scsi_read_capacity` 失败，未注册 `sda`。
4. `sda` 已注册，但 block/devtmpfs 路径没有为其生成 `/dev/sda` 节点。

## Plan
- 第一步只加最小运行时插桩，分别覆盖 PCI match/probe、virtio queue 初始化、SCSI 扫描、disk 注册四个观察点。
- 根据串口日志判定命中的假设，再做最小修复。

## Evidence
- pre-fix:
  - `virtscsi: probe ...`
  - `virtscsi: vq0/vq1/vq2 ...`
  - `virtscsi: cdb=0x0 ... status=2 key=6 asc=41 ascq=0`
  - `virtio-scsi` smoke 两个测试都被 skip
- analysis:
  - 假设 1 被否决：PCI probe 已命中
  - 假设 2 被否决：virtqueue 初始化成功
  - 假设 3 被确认：第一次 TUR 命中 `UNIT ATTENTION`
  - 随后又发现 `INQUIRY` response header 被破坏，对照 Linux `drivers/scsi/virtio_scsi.c` 确认是描述符链顺序错误

## Fix
- 对 `TEST UNIT READY` 做有限次重试，吸收第一次 `UNIT ATTENTION`
- 按 Linux 的 virtio-scsi 顺序修正 descriptor chain：
  - `REQ`
  - `DATA-OUT`
  - `RESP`
  - `DATA-IN`

## Post-fix
- `virtscsi: TUR attempt=2 ret=0`
- `virtscsi: inquiry ret=0`
- `virtscsi: read_capacity ret=0`
- `virtscsi: scsi_add_disk ret=0`
- smoke:
  - `Virtio-SCSI Device -- virtio-scsi disk detected OK.`
  - `Virtio-SCSI Raw R/W -- virtio-scsi raw rw OK.`

## Cleanup
- 临时运行时插桩已从 `drivers/scsi/virtio_scsi.c` 移除。
- 保留的正式修复只有两项：
  - `TEST UNIT READY` 的有限次重试
  - Linux 风格的 virtio-scsi descriptor chain 顺序
