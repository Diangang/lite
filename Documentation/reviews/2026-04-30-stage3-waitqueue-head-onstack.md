# Stage 3 Review: on-stack waitqueue head declaration

Patch: `stage3-waitqueue-head-onstack`

## Linux Alignment Report

Change scope:
- Files: `include/linux/wait.h`
- Directories: `include/linux`
- Public surface: `DECLARE_WAIT_QUEUE_HEAD_ONSTACK(name)`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/wait.h`
- Linux symbols: `__WAIT_QUEUE_HEAD_INIT_ONSTACK`, `DECLARE_WAIT_QUEUE_HEAD_ONSTACK`
- Lite file: `include/linux/wait.h`
- This step only changes: add Linux's on-stack waitqueue head declaration macros.

Mapping ledger:
- Functions: none
- Structs:
  - `wait_queue_head_t`: `linux2.6/include/linux/wait.h::wait_queue_head_t`, lite=`include/linux/wait.h`, placement=OK
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/wait.h`, lite=`include/linux/wait.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, macro names match Linux.
- Placement: OK, macros are in `include/linux/wait.h` with waitqueue head initialization helpers.
- Semantics: OK, `CONFIG_LOCKDEP` uses runtime initialization and the non-lockdep branch aliases `DECLARE_WAIT_QUEUE_HEAD()`.
- Flow/Lifetime: OK, declaration-time helper only.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed after one transient QEMU timeout at NVMe minixfs.
- `make smoke-512`: passed after two transient QEMU/NVMe timeout runs.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h state.json Documentation/reviews/2026-04-30-stage3-waitqueue-head-onstack.md`

Findings: none.
