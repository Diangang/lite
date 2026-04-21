[OPEN] scsi-nvme-smoke

## Symptom
- `make smoke-512` now starts QEMU with `virtio-scsi + nvme0 + nvme1`.
- Runtime evidence shows:
  - `nvme0n1 ready`
  - `nvme1n1 ready`
  - boot later halts at `vfs_get_sb_single: fill_super failed`
- Expected:
  - SCSI and NVMe paths both present in one boot
  - raw R/W passes for `/dev/sda` and `/dev/nvme1n1`
  - MinixFS mount + R/W passes for both SCSI-backed and NVMe-backed paths

## Current Hypotheses
1. `/mnt` is now mounted from `/dev/sda`, but `scsi.img` is not a valid MinixFS image, so `minix fill_super` fails.
2. `virtio-scsi` enumeration under `q35 + nvme` is delayed, so `/dev/sda` is not ready when `prepare_namespace()` mounts `/mnt`.
3. The first failing mount is `/mnt_nvme`, not `/mnt`, and the panic obscures which device actually failed.
4. `nvme0.img`/`nvme1.img` are not MinixFS-formatted, so mounting NVMe as MinixFS during boot must fail unless we prepare a valid image.
5. `minix_seed_example_image()` was only applied to `ram1` previously; the new SCSI/NVMe paths rely on unseeded media and therefore fail in `fill_super`.

## Constraints
- Follow Linux-shaped terminology and mount semantics.
- Avoid speculative logic rewrites before identifying which mount/device fails.
- First code changes in this debug phase should be instrumentation only.

## Next Evidence To Collect
- Identify which `vfs_mount_fs_dev()` call fails: `/mnt` or `/mnt_nvme`.
- Identify the exact `dev_name` passed into `minixfs->get_sb()/fill_super`.
- Confirm whether `/dev/sda`, `/dev/nvme0n1`, and `/dev/nvme1n1` exist before mount.
- Confirm whether `scsi.img` is already a MinixFS image and whether NVMe images are blank.
