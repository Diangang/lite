# Review: stage3-add-wait-queue-tail-helper

Final commit: see `git log -1`.
Pre-review commit: 9694baf74fe5e98234a47ac68f09a98536933620

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__add_wait_queue_tail`
- Lite target: `include/linux/wait.h::__add_wait_queue_tail`
- Single difference: Lite now provides Linux's low-level waitqueue tail add helper.

Linux 2.6 exposes `__add_wait_queue_tail()` as an inline wrapper around
`list_add_tail()` on the wait entry's `task_list`. Lite now provides the same
helper over its existing waitqueue layout without changing current callers.

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
