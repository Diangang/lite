# Review: stage3-add-wait-queue-tail-exclusive-helper

Final commit: see `git log -1`.
Pre-review commit: 78f8189047f34135355550f1e045f7d41daa9c11

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__add_wait_queue_tail_exclusive`
- Lite target: `include/linux/wait.h::__add_wait_queue_tail_exclusive`
- Single difference: Lite now provides Linux's low-level exclusive waitqueue tail add helper.

Linux 2.6 exposes `__add_wait_queue_tail_exclusive()` as an inline helper that
sets `WQ_FLAG_EXCLUSIVE` on the wait entry and appends it through
`__add_wait_queue_tail()`. Lite now provides the same helper over the existing
waitqueue layout without changing current callers.

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
