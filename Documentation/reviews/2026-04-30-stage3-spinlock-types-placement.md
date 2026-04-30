# Stage 3 Review: spinlock types placement

Patch: `stage3-spinlock-types-placement`

## Linux Alignment Report

Change scope:
- Files: `include/linux/spinlock.h`, `include/linux/spinlock_types.h`
- Directories: `include/linux`
- Public surface: `raw_spinlock_t`, `spinlock_t`, spinlock initializer macros

Reference-first evidence:
- Linux files: `linux2.6/include/linux/spinlock.h`, `linux2.6/include/linux/spinlock_types.h`
- Linux symbols: `raw_spinlock_t`, `spinlock_t`, `__RAW_SPIN_LOCK_UNLOCKED`, `__SPIN_LOCK_UNLOCKED`, `DEFINE_SPINLOCK`, `DEFINE_RAW_SPINLOCK`
- Lite files: `include/linux/spinlock.h`, `include/linux/spinlock_types.h`
- This step only changes: move Lite spinlock type declarations and initializer macros to the Linux corresponding header.

Mapping ledger:
- Functions: none
- Structs:
  - `raw_spinlock_t`: `linux2.6/include/linux/spinlock_types.h::raw_spinlock_t`, lite=`include/linux/spinlock_types.h`, placement=OK
  - `spinlock_t`: `linux2.6/include/linux/spinlock_types.h::spinlock_t`, lite=`include/linux/spinlock_types.h`, placement=OK
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/spinlock_types.h`, lite=`include/linux/spinlock_types.h`, placement=OK
  - `linux2.6/include/linux/spinlock.h`, lite=`include/linux/spinlock.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, type and macro names are unchanged.
- Placement: OK, type declarations now live in `include/linux/spinlock_types.h`.
- Semantics: OK, definitions are moved without changing fields or initializer values.
- Flow/Lifetime: OK, header placement only.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/spinlock.h include/linux/spinlock_types.h state.json Documentation/reviews/2026-04-30-stage3-spinlock-types-placement.md`

Findings: none.
