# Review: stage3-add-wait-queue-inline-helper

Final commit: see `git log -1`.
Pre-review commit: 42072c5cc0f7cabb246cff6fd63b7f11a33ed82d

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__add_wait_queue`
- Lite target: `include/linux/wait.h::__add_wait_queue`
- Single difference: Lite now provides Linux's low-level waitqueue add helper.

Linux 2.6 exposes `__add_wait_queue()` as an inline wrapper around
`list_add()` on the wait entry's `task_list`. Lite now provides the same helper
over its existing waitqueue entry and head layout without changing existing
`add_wait_queue()` callers.

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
