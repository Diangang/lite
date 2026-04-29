# Review: stage2-default-ramdisk-init

Final commit: see `git log -1`.
Pre-review commit: a7514c59558285f066b35c41312c4be4e83c62e7

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::kernel_init_freeable`
- Linux reference: `linux2.6/init/main.c::ramdisk_execute_command`
- Lite target: `init/main.c::prepare_namespace`
- Lite target: `init/main.c::ramdisk_execute_command`
- Single difference: Lite now probes `/init` as the default early userspace init when no explicit `rdinit=` is present.

Linux defaults `ramdisk_execute_command` to `/init` and falls back to namespace
mounting when it is absent. Lite performs the same default probe after rootfs
population and before regular `init=` and fallback init paths. The current
initramfs has no `/init`, so existing smoke boots still enter `/sbin/init`.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe raw test.

## Findings

None.
