# Review: stage3-add-wait-queue-exclusive-api

Final commit: see `git log -1`.
Pre-review commit: fc107c205d3f5f97a58996117326c6167ea7baaf

## Scope

- `include/linux/wait.h`
- `kernel/sched/wait.c`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::add_wait_queue_exclusive`
- Linux reference: `linux2.6/kernel/sched/wait.c::add_wait_queue_exclusive`
- Lite target: `include/linux/wait.h::add_wait_queue_exclusive`
- Lite target: `kernel/sched/wait.c::add_wait_queue_exclusive`
- Single difference: Lite now exposes Linux's public exclusive waitqueue add API.

Linux 2.6 declares `add_wait_queue_exclusive()` in `include/linux/wait.h` and
implements it in `kernel/sched/wait.c` by setting `WQ_FLAG_EXCLUSIVE`, taking the
waitqueue lock with irqsave, adding the entry at the tail, and restoring the lock.
Lite now provides the same API over its existing waitqueue and spinlock helpers.

Lite DIFF: the implementation keeps the local null guards used by Lite's current
waitqueue API entry points.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h kernel/sched/wait.c state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
