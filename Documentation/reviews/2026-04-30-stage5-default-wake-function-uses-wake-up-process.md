# Stage 5 Review: default_wake_function uses wake_up_process

## Linux Alignment Report

Change scope:
- files: `kernel/sched/wait.c`
- directories: `kernel/sched/`
- public surface: none

Reference-first evidence:
- Linux file: `linux2.6/kernel/sched/core.c`
- Linux symbol: `default_wake_function`
- Linux symbol: `try_to_wake_up`
- Lite file: `kernel/sched/wait.c`
- This step only changes: Lite's waitqueue default wake function delegates the runnable transition to `wake_up_process()` instead of duplicating scheduler state changes locally.

Mapping ledger:
- functions:
  - `default_wake_function`: `linux2.6/kernel/sched/core.c::default_wake_function`, lite=`kernel/sched/wait.c`, placement=DIFF
  - `wake_up_process`: `linux2.6/kernel/sched/core.c::wake_up_process`, lite=`kernel/sched/core.c`, placement=OK
- structs:
  - `wait_queue_entry`: `linux2.6/include/linux/wait.h::wait_queue_t`, lite=`include/linux/wait.h`, placement=OK
- globals/statics: none
- files:
  - `linux2.6/kernel/sched/core.c` -> `kernel/sched/core.c`, placement=OK for scheduler wake helper
  - `kernel/sched/wait.c` has Lite's existing waitqueue implementation, placement=DIFF for Linux's `default_wake_function`
- directories:
  - `linux2.6/kernel/sched/` -> `kernel/sched/`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: DIFF -> Lite keeps waitqueue implementation in `kernel/sched/wait.c`; this patch does not move files.
- Semantics: OK -> Lite still clears the waitqueue attachment and wakes blocked waiters, while using the scheduler wake API for the state transition.
- Flow/Lifetime: OK

If DIFF:
- Why: Lite already split waitqueue code into `kernel/sched/wait.c`.
- Impact: Waitqueue wake behavior remains stable while local duplicated scheduler wake logic is removed.
- Plan: Keep placement unchanged for this patch and replace the local blocked-state transition with `wake_up_process(task)`.

## Validation

- `make -j4` passed.
- `make smoke-128` passed.
- `make smoke-512` passed.

## Review

Clean.

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- kernel/sched/wait.c state.json Documentation/reviews/2026-04-30-stage5-default-wake-function-uses-wake-up-process.md`
