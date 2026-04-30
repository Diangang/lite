# Stage 3 Review: spinlock before barrier

Patch: `stage3-spinlock-before-barrier`

## Linux Alignment Report

Change scope:
- Files: `include/linux/spinlock.h`
- Directories: `include/linux`
- Public surface: `smp_mb__before_spinlock()`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/spinlock.h`
- Linux symbol: `smp_mb__before_spinlock`
- Lite file: `include/linux/spinlock.h`
- This step only changes: add Linux's default pre-spinlock ordering helper.

Mapping ledger:
- Functions: none
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/spinlock.h`, lite=`include/linux/spinlock.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, macro name matches Linux.
- Placement: OK, macro is in `include/linux/spinlock.h` with spinlock definitions.
- Semantics: OK, default maps to `smp_wmb()` as in Linux.
- Flow/Lifetime: OK, ordering helper only.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after one transient QEMU timeout at NVMe minixfs.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/spinlock.h state.json Documentation/reviews/2026-04-30-stage3-spinlock-before-barrier.md`

Findings: none.
