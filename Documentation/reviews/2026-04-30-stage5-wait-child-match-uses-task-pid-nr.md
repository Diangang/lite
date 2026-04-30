# Stage 5 Review: Wait Child Match Uses Task PID Helper

## Scope

- Patch: `stage5-wait-child-match-uses-task-pid-nr`
- Lite file: `kernel/sched/wait.c`
- Linux references:
  - `linux2.6/kernel/exit.c`
  - `linux2.6/include/linux/sched.h`

## Linux Alignment Report

Change scope:
- files: `kernel/sched/wait.c`
- directories: `kernel/sched`
- public surface: `waitpid(id)` child target matching

Reference-first evidence:
- Linux `eligible_pid()` matches wait targets through `task_pid_type(p, wo->wo_type)` instead of direct pid member comparison in the wait path.
- Lite already has `task_pid_nr()` as the available task pid helper.
- This step only changes the child target comparison in `do_waitpid()` from `t->pid == id` to `task_pid_nr(t) == id`.

Mapping ledger:
- functions:
  - `eligible_pid`: `linux2.6/kernel/exit.c::eligible_pid`, lite=`kernel/sched/wait.c::do_waitpid`, placement=DIFF because Lite keeps compact wait handling in `kernel/sched/wait.c`.
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr/task_pid_vnr`, lite=`include/linux/sched.h::task_pid_nr`, placement=OK.
- structs:
  - `task_struct`: Linux and Lite task identity carrier, placement=OK.
- globals/statics: none.
- NO_DIRECT_LINUX_MATCH: none.

Consistency:
- Naming: OK, uses existing Linux-named `task_pid_nr()`.
- Placement: DIFF, accepted as existing Lite wait placement; no file movement in this patch.
- Semantics: OK, positive child pid matching is unchanged except for helper access.
- Flow/Lifetime: OK, no waitqueue, tasklist lock, or reaping behavior changed.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

- Findings: none.
- Notes: The parent self-id guard remains a separate direct pid read and can be aligned independently.
