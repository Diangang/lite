# Stage 5: proc task dump uses task_pid_nr

## Linux Alignment Report

Change scope:
- files: `fs/proc/array.c`
- directories: `fs/proc`
- public surface: Lite proc task listing PID output

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbol: `task_pid_nr`
- Linux evidence: `task_pid_nr(struct task_struct *tsk)` is the helper for exposing a task's PID number.
- Linux file: `linux2.6/fs/proc/array.c`
- Linux evidence: proc reporting paths use task PID helpers for task identity fields.
- Lite file: `fs/proc/array.c`
- This step only changes: replace the direct `task->pid` output in `task_dump_tasks()` with `task_pid_nr(task)`.

Mapping ledger:
- functions:
  - `task_dump_tasks`: Linux=`NO_DIRECT_LINUX_MATCH`, lite=`fs/proc/array.c::task_dump_tasks`, placement=NO_DIRECT_LINUX_MATCH
  - `task_pid_nr`: Linux=`linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h::task_pid_nr`, placement=OK
- structs:
  - `task_struct`: Linux=`linux2.6/include/linux/sched.h::task_struct`, lite=`include/linux/sched.h::task_struct`, placement=OK
- globals/statics: none
- files:
  - `fs/proc/array.c`: placement=OK
- directories:
  - `fs/proc`: placement=OK
- NO_DIRECT_LINUX_MATCH:
  - `task_dump_tasks` is a Lite proc task listing helper; the PID accessor substitution still maps to Linux's task PID helper.

Consistency:
- Naming: OK, uses Linux helper name.
- Placement: OK for proc and scheduler helper files.
- Semantics: OK, PID output remains unchanged for Lite.
- Flow/Lifetime: OK, no task iteration or locking changes.

If DIFF:
- Why: Lite has a minimal `/proc/tasks` style listing helper not present as a direct Linux function.
- Impact: no ABI value change; the PID field is produced through the Linux-shaped accessor.
- Plan: keep later remaining direct proc PID outputs as separate narrow patches.

## Patch Summary

- Changed `task_dump_tasks()` to call `task_pid_nr(task)` instead of reading `task->pid` directly.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Findings: none.

Residual risk:
- None specific to this accessor substitution.
