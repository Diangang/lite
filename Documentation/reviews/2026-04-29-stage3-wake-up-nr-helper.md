# Review: stage3-wake-up-nr-helper

Final commit: see `git log -1`.
Pre-review commit: fb10db8c5a668fc56f5217c3682e29b21ce0d526

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::wake_up_nr`
- Lite target: `include/linux/wait.h::wake_up_nr`
- Single difference: Lite now provides Linux's numbered waitqueue wake helper.

Linux 2.6 defines `wake_up_nr(x, nr)` as a wrapper over `__wake_up()` with the
caller-provided exclusive wake count. Lite now provides the same helper shape
over its existing `__wake_up()` backend.

Lite DIFF: Linux passes `TASK_NORMAL`; Lite waitqueue wake helpers currently use
mode `0` because Lite has not introduced Linux's task-state mask constants yet
and the existing `__wake_up()` implementation ignores `mode`.

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
