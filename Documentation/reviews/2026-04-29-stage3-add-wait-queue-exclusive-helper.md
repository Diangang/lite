# Review: stage3-add-wait-queue-exclusive-helper

Final commit: see `git log -1`.
Pre-review commit: ab40942adf73b9538aafa809886eb250ed57457b

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__add_wait_queue_exclusive`
- Lite target: `include/linux/wait.h::__add_wait_queue_exclusive`
- Single difference: Lite now provides Linux's exclusive low-level waitqueue add helper.

Linux 2.6 marks a wait entry with `WQ_FLAG_EXCLUSIVE` and then delegates to
`__add_wait_queue()`. Lite now provides the same helper over the already
aligned flag constant and low-level add helper without changing current
waitqueue users.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount/write test.
- `make smoke-512`: passed

## Findings

None.
