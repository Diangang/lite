# Stage 5 Review: Do Exit Uses Is Idle Task

## Scope

- Patch: `stage5-do-exit-uses-is-idle-task`
- Lite file: `kernel/exit.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/exit.c`
- directories: `kernel`
- public surface: none; internal exit guard only.

Reference-first evidence:
- Linux `do_exit()` explicitly protects pid-zero idle task.
- Linux defines `is_idle_task(const struct task_struct *p)` in `include/linux/sched.h` for pid-zero idle task checks.
- Lite `do_exit()` already checks for a null task before the idle-task guard.
- This step only changes `do_exit()` from direct `task->pid == 0` to `is_idle_task(task)`.

Mapping ledger:
- functions:
  - `do_exit`: `linux2.6/kernel/exit.c::do_exit`, lite=`kernel/exit.c::do_exit`, placement=OK.
  - `is_idle_task`: `linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, uses existing Linux-named `is_idle_task()`.
- Placement: OK.
- Semantics: OK, idle task still cannot exit.
- Flow/Lifetime: OK, delegation to `do_exit_reason()` remains unchanged.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: Remaining pid-zero guards in `do_exit_reason()` and `task_release_invariant_holds()` remain separate patch candidates.
