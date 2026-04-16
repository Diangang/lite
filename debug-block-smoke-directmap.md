# [OPEN] Debug Session: block-smoke-directmap

## Symptom
- `make smoke-512` boots QEMU, then panics with `directmap_phys_to_virt: phys out of range`.
- Regression observed after recent Block Layer alignment work.

## Hypotheses
1. Reading `/sys/class/block/*/stat` triggers a lazy bdev/sysfs/pagecache path that touches an invalid physical address.
2. New `request_queue` accounting (`nr_requests/queued/in_flight`) corrupts queue state and triggers an unexpected I/O path.
3. Recent bdev inode teardown/pagecache release work corrupts mapping state before userspace smoke runs.
4. The panic is unrelated to the new Block Layer logic and is an existing boot/runtime instability exposed by current image/layout.

## Evidence Plan
- Add minimal panic-site instrumentation to capture the out-of-range physical address and lowmem boundary.
- Add boot-stage instrumentation around `/bin/smoke` launch path only if panic happens after init/userspace starts.
- Re-run `make smoke-512` and compare captured values against the hypotheses.

## Status
- Instrumentation result:
  - `DEBUG directmap-range phys=0xe9003000 lowmem_end=0x20000000 total_pages=131038`
  - Interpretation: caller passed a PCI MMIO / vmalloc-range address to `memlayout_directmap_phys_to_virt()`, not a lowmem physical page.
- Confirmed hypothesis:
  - H4 refined: failure is outside the new block queue accounting itself; the current boot path reaches modern virtio-pci capability parsing, which mis-maps BAR MMIO through the directmap helper.
- Fix under test:
  - Switch modern virtio-pci capability mapping from `memlayout_directmap_phys_to_virt()` to `ioremap()`.
  - Fix `ioremap()` semantics for non-page-aligned MMIO physical addresses (align phys down, map full span, return virtual address plus page offset).
- Post-fix verification:
  - `make -j4`: PASS
  - `make smoke-512`: PASS
  - No repeated `directmap-range` panic seen after the fix; virtio-scsi initcall completes and the smoke image reaches success.
