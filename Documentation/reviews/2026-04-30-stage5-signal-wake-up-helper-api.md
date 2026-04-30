# Stage 5 Review: signal wake helper API

## Linux Alignment Report

Change scope:
- files: `kernel/signal.c`, `include/linux/sched.h`
- directories: `kernel/`, `include/linux/`
- public surface: add the Linux-named `signal_wake_up_state()` declaration to `include/linux/sched.h`

Reference-first evidence:
- Linux file: `linux2.6/kernel/signal.c`
- Linux symbol: `signal_wake_up_state`
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `signal_wake_up_state`
- Lite file: `kernel/signal.c`, `include/linux/sched.h`
- This step only changes: Lite's private `signal_wake_task()` helper is replaced by the Linux-named `signal_wake_up_state()` API and the local caller is updated.

Mapping ledger:
- functions:
  - `signal_wake_up_state`: `linux2.6/kernel/signal.c::signal_wake_up_state`, lite=`kernel/signal.c`, placement=OK
  - `signal_wake_up_state` declaration: `linux2.6/include/linux/sched.h::signal_wake_up_state`, lite=`include/linux/sched.h`, placement=OK
  - `sys_kill`: `linux2.6/kernel/signal.c::sys_kill`, lite=`kernel/signal.c`, placement=OK
- structs: none
- globals/statics: removes Lite-only static helper `signal_wake_task`
- files:
  - `linux2.6/kernel/signal.c` -> `kernel/signal.c`, placement=OK
  - `linux2.6/include/linux/sched.h` -> `include/linux/sched.h`, placement=OK
- directories:
  - `linux2.6/kernel/` -> `kernel/`, placement=OK
  - `linux2.6/include/linux/` -> `include/linux/`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: DIFF -> Lite uses private `signal_wake_task`; Linux uses `signal_wake_up_state`.
- Placement: OK
- Semantics: OK -> Lite keeps its existing simplified wake behavior and ignores the Linux state mask until the scheduler state model is broadened.
- Flow/Lifetime: OK

If DIFF:
- Why: Lite had the right local behavior behind non-Linux helper vocabulary.
- Impact: Existing `sys_kill(SIGCHLD)` wake behavior is preserved while the helper surface becomes Linux-shaped for later signal work.
- Plan: Replace the private helper with `signal_wake_up_state()` in `kernel/signal.c`, declare it from `include/linux/sched.h`, and update the local call site.

## Validation

- `make -j4` passed.
- `make smoke-128` passed.
- `make smoke-512` passed.

## Review

Clean.

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- kernel/signal.c include/linux/sched.h state.json Documentation/reviews/2026-04-30-stage5-signal-wake-up-helper-api.md`
