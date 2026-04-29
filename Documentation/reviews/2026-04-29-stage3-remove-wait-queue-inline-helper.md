# Review: stage3-remove-wait-queue-inline-helper

Final commit: see `git log -1`.
Pre-review commit: 79e747c1f55adafb323241d3c3825cce539ef620

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__remove_wait_queue`
- Lite target: `include/linux/wait.h::__remove_wait_queue`
- Single difference: Lite now provides Linux's low-level waitqueue remove helper.

Linux 2.6 exposes `__remove_wait_queue()` as an inline wrapper around
`list_del()` on the wait entry's `task_list`. Lite now provides the same helper
over its existing waitqueue and list layout without changing current callers.

Lite DIFF: the helper marks `head` unused with `(void)head;` because Lite builds
headers with `-Wextra`, while the Linux reference leaves that parameter unused.

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
