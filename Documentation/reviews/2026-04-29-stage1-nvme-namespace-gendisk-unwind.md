# Review: stage1-nvme-namespace-gendisk-unwind

Final commit: see `git log -1`.
Pre-review commit: d33edaa45ea0418fe709f7be06f5fe985643c8f4

## Scope

- `drivers/nvme/host/pci.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/drivers/nvme/host/pci.c::nvme_alloc_ns`
- Lite target: `drivers/nvme/host/pci.c::nvme_ns_init`
- Single difference: failure unwinding after namespace gendisk initialization now uses the Lite gendisk release helper instead of open-coded freeing.

Linux `nvme_alloc_ns()` unwinds namespace setup through explicit disk, queue,
and namespace release labels. Lite does not have `alloc_disk_node()`, but its
registered gendisk lifetime is released through `put_disk()`, also used by
`nvme_ns_remove()` and the block ramdisk `add_disk()` failure path.

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
