# Review: stage2-rdinit-command-line

Final commit: see `git log -1`.
Pre-review commit: 2bb1e67d8454adf53672070a887d299a28647358

## Scope

- `init/main.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/init/main.c::ramdisk_execute_command`
- Linux reference: `linux2.6/init/main.c::rdinit_setup`
- Linux reference: `linux2.6/init/main.c::kernel_init`
- Lite target: `init/main.c::ramdisk_execute_command`
- Lite target: `init/main.c::parse_command_line`
- Lite target: `init/main.c::prepare_namespace`
- Single difference: Lite now recognizes `rdinit=` and attempts that command before regular `init=` and fallback init paths.

Linux stores `rdinit=` in `ramdisk_execute_command` and tries it before
`execute_command`. Lite now follows that ordering for explicit `rdinit=`, while
leaving Linux's default `/init` probing for a later, separate alignment step.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- init/main.c include/linux/init.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount test.
- `make smoke-512`: passed after rerun; an earlier run hit an NVMe read timeout during namespace mount.

## Findings

None.
