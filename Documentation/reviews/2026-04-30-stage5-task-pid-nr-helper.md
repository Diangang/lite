# Stage 5 Review: task_pid_nr helper

Patch: `stage5-task-pid-nr-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`
- Directories: `include/linux`
- Public surface: `task_pid_nr`, `task_tgid_nr`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbols: `task_pid_nr`, `task_tgid_nr`
- Lite file: `include/linux/sched.h`
- This step only changes: add the Linux task PID accessor and route Lite's no-thread-group TGID accessor through it.

Mapping ledger:
- Functions:
  - `task_pid_nr`: `linux2.6/include/linux/sched.h::task_pid_nr`, lite=`include/linux/sched.h`, placement=OK
  - `task_tgid_nr`: `linux2.6/include/linux/sched.h::task_tgid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, the new helper uses the Linux symbol name.
- Placement: OK.
- Semantics: OK as a Lite subset; Linux returns `tsk->pid`, and Lite returns PID for non-NULL tasks while keeping existing local null-guard behavior.
- Flow/Lifetime: OK, accessor-only change.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed on rerun

Note:
- The first `make smoke-512` attempt hit the smoke script's 30 second hard timeout during NVMe raw R/W and returned `make` error 2 without a smoke `FAIL` summary. The same command passed on immediate rerun.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/sched.h state.json Documentation/reviews/2026-04-30-stage5-task-pid-nr-helper.md`

Findings: none.
