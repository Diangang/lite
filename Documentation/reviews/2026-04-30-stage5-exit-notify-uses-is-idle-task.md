# Stage 5 Review: Exit Notify Uses Is Idle Task

## Scope

- Patch: `stage5-exit-notify-uses-is-idle-task`
- Lite file: `kernel/exit.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/exit.c`
- directories: `kernel`
- public surface: none; internal exit notification guard only.

Reference-first evidence:
- Linux defines `is_idle_task(const struct task_struct *p)` in `include/linux/sched.h` for pid-zero idle task checks.
- Linux `do_exit()` treats pid zero as the idle task.
- Lite `exit_notify()` already checks for a null task before the idle-task guard.
- This step only changes `exit_notify()` from direct `task->pid == 0` to `is_idle_task(task)`.

Mapping ledger:
- functions:
  - `is_idle_task`: `linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK.
  - `exit_notify`: `linux2.6/kernel/exit.c::exit_notify`, lite=`kernel/exit.c::exit_notify`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none for this guard concept.

Consistency:
- Naming: OK, uses existing Linux-named `is_idle_task()`.
- Placement: OK.
- Semantics: OK, idle task still returns before exit notification.
- Flow/Lifetime: OK, no reparent, wake, or zombify ordering changed.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: Remaining pid-zero guards in `release_task()`, `do_exit()`, `do_exit_reason()`, and `task_release_invariant_holds()` remain separate patch candidates.
