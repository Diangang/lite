# Stage 5 Review: wake_up_process returns int

## Linux Alignment Report

Change scope:
- files: `include/linux/sched.h`, `kernel/sched/core.c`
- directories: `include/linux/`, `kernel/sched/`
- public surface: change `wake_up_process()` from `void` to Linux's `int` return type

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `wake_up_process`
- Linux file: `linux2.6/kernel/sched/core.c`
- Linux symbol: `wake_up_process`
- Lite file: `include/linux/sched.h`, `kernel/sched/core.c`
- This step only changes: `wake_up_process()` reports whether it woke a sleeping or blocked task, while preserving existing wake behavior.

Mapping ledger:
- functions:
  - `wake_up_process`: `linux2.6/kernel/sched/core.c::wake_up_process`, lite=`kernel/sched/core.c`, placement=OK
  - `wake_up_process` declaration: `linux2.6/include/linux/sched.h::wake_up_process`, lite=`include/linux/sched.h`, placement=OK
- structs: none
- globals/statics: none
- files:
  - `linux2.6/kernel/sched/core.c` -> `kernel/sched/core.c`, placement=OK
  - `linux2.6/include/linux/sched.h` -> `include/linux/sched.h`, placement=OK
- directories:
  - `linux2.6/kernel/sched/` -> `kernel/sched/`, placement=OK
  - `linux2.6/include/linux/` -> `include/linux/`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: DIFF -> Lite returns no wake result; Linux returns 1 if the task was woken, 0 otherwise.
- Flow/Lifetime: OK

If DIFF:
- Why: Lite had the wake transition behavior but not Linux's return-value API.
- Impact: Existing callers keep ignoring the result; future wait/signal code can use Linux-shaped success reporting.
- Plan: Change the declaration and implementation return type to `int`, return `1` after a sleeping/blocked task is made runnable, and return `0` for null or already-runnable/non-wakeable tasks.

## Validation

- `make -j4` passed.
- `make smoke-128` passed.
- `make smoke-512` passed.

## Review

Clean.

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/sched.h kernel/sched/core.c state.json Documentation/reviews/2026-04-30-stage5-wake-up-process-return-int.md`
