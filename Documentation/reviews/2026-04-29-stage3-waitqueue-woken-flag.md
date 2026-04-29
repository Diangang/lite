# Review: stage3-waitqueue-woken-flag

Final commit: see `git log -1`.
Pre-review commit: fa73153d02367ebd3d8cb2d26a0b094898b71c11

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h`
- Lite target: `include/linux/wait.h`
- Single difference: Lite now defines Linux's `WQ_FLAG_WOKEN` waitqueue flag value.

Linux 2.6 defines `WQ_FLAG_WOKEN` as `0x02` next to `WQ_FLAG_EXCLUSIVE`.
Lite now carries the same constant in the same header so later waitqueue
helper alignment can use the Linux flag name and value directly.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
