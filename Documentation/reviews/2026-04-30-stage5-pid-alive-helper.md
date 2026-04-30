# Stage 5 Review: pid_alive helper

Patch: `stage5-pid-alive-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/sched.h`
- Directories: `include/linux`
- Public surface: `pid_alive`, `task_ppid_nr`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/sched.h`
- Linux symbols: `pid_alive`, `task_ppid_nr`
- Lite file: `include/linux/sched.h`
- This step only changes: add Linux's stale-task predicate and use it in `task_ppid_nr`.

Mapping ledger:
- Functions:
  - `pid_alive`: `linux2.6/include/linux/sched.h::pid_alive`, lite=`include/linux/sched.h`, placement=OK
  - `task_ppid_nr`: `linux2.6/include/linux/sched.h::task_ppid_nr`, lite=`include/linux/sched.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/sched.h`, lite=`include/linux/sched.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, helper names match Linux.
- Placement: OK.
- Semantics: OK subset. Lite has no `struct pid` array, so tasklist membership is the PID visibility predicate.
- Flow/Lifetime: OK. `release_task()` removes tasklist linkage before the final task reference drop.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed after retry; first run hit a transient NVMe read timeout during mount before the test suite started
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/sched.h state.json Documentation/reviews/2026-04-30-stage5-pid-alive-helper.md`

Findings: none.
