# Review: stage1-nvme-success-debug-print-cleanup

Final commit: see `git log -1`.
Pre-review commit: 16069d2f849fc0221d47c0c3bd8b8e9ed868d26c

## Scope

- `drivers/nvme/host/pci.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/drivers/nvme/host/pci.c::nvme_dev_map`
- Linux reference: `linux2.6/drivers/nvme/host/pci.c::set_queue_count`
- Lite target: `drivers/nvme/host/pci.c::nvme_map_mmio`
- Lite target: `drivers/nvme/host/pci.c::nvme_dev_init`
- Lite target: `drivers/nvme/host/pci.c::nvme_create_io_queues`
- Single difference: remove success-path NVMe bring-up register and queue-count debug prints.

Linux keeps the NVMe setup success path quiet except for targeted warnings or
errors. Lite now keeps failure diagnostics and the final namespace-ready line,
but no longer prints BAR0, CAP, MQES, DSTRD, or negotiated queue count on every
successful boot.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- drivers/nvme/host/pci.c state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed on exact rerun; first run was terminated by the smoke harness during NVMe MinixFS without a printed failure.
- `make smoke-512`: passed

## Findings

None.
