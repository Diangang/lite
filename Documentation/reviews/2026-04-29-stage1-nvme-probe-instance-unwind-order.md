# Review: stage1-nvme-probe-instance-unwind-order

Final commit: see `git log -1`.
Pre-review commit: e70f3eeaacb857b813738d5e51fcc4690f0ce044

## Scope

- `drivers/nvme/host/pci.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/drivers/nvme/host/pci.c::nvme_probe`
- Linux reference: `linux2.6/drivers/nvme/host/pci.c::nvme_free_dev`
- Lite target: `drivers/nvme/host/pci.c::nvme_probe`
- Single difference: probe failure unwind releases the allocated NVMe instance before freeing the controller wrapper object.

Linux releases the instance while the controller object is still live, either
through probe error labels or `nvme_free_dev()`. Lite keeps explicit instance
release outside `nvme_free_dev()`, so the probe error paths now preserve that
ordering without adding kref or other missing Linux infrastructure.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- drivers/nvme/host/pci.c state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
