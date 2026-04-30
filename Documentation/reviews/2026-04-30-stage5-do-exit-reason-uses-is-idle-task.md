# Stage 5 Review: Do Exit Reason Uses Is Idle Task

## Scope

- Patch: `stage5-do-exit-reason-uses-is-idle-task`
- Lite file: `kernel/exit.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/exit.c`
- directories: `kernel`
- public surface: none; internal exit-reason guard only.

Reference-first evidence:
- Linux `do_exit()` explicitly protects pid-zero idle task before exit work.
- Linux defines `is_idle_task(const struct task_struct *p)` in `include/linux/sched.h` for pid-zero idle task checks.
- Lite `do_exit_reason()` already checks for a null task before the idle-task guard.
- This step only changes `do_exit_reason()` from direct `task->pid == 0` to `is_idle_task(task)`.

Mapping ledger:
- functions:
  - `do_exit`: `linux2.6/kernel/exit.c::do_exit`, lite=`kernel/exit.c::do_exit_reason`, placement=DIFF because Lite splits reason-carrying exit into a helper.
  - `is_idle_task`: `linux2.6/include/linux/sched.h::is_idle_task`, lite=`include/linux/sched.h::is_idle_task`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH:
  - Why: Lite carries exit reason metadata through a compact helper while Linux encodes exit state in the broader `do_exit()` path.
  - Impact: The patch changes only idle-task guard spelling, not exit notification flow.
  - Plan: Continue converting remaining pid-zero guards separately where the Linux idle-task concept applies.

Consistency:
- Naming: OK, uses existing Linux-named `is_idle_task()`.
- Placement: DIFF accepted for the existing Lite helper split.
- Semantics: OK, idle task still cannot exit with reason.
- Flow/Lifetime: OK, delegation to `exit_notify()` is unchanged.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: `state.json` was tightened to make final-answer stopping illegal unless an allowed stop condition or explicit latest user pause/stop/report-only request exists.
