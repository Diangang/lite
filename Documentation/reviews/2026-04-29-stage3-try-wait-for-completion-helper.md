# Review: stage3-try-wait-for-completion-helper

Final commit: see `git log -1`.
Pre-review commit: 218bb2fe6eb2b2465c2e52da32301831c061dccc

## Scope

- `include/linux/completion.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/kernel/sched/completion.c::try_wait_for_completion`
- Linux reference: `linux2.6/include/linux/completion.h::try_wait_for_completion`
- Lite target: `include/linux/completion.h::try_wait_for_completion`
- Single difference: Lite now provides Linux's non-blocking completion decrement helper.

Linux 2.6 first checks `done` without taking the lock, then rechecks under the
completion wait lock before decrementing. Lite keeps completion helpers inline
and uses IRQ save/restore for the existing completion critical sections; the
new helper follows the same two-step check and returns `bool` like Linux.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/completion.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe raw test.

## Findings

None.
