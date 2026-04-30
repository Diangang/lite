# Stage 3 Review: raw spin contended fallback

Patch: `stage3-raw-spin-is-contended`

## Linux Alignment Report

Change scope:
- Files: `include/linux/spinlock.h`
- Directories: `include/linux`
- Public surface: `raw_spin_is_contended(lock)`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/spinlock.h`
- Linux symbol: `raw_spin_is_contended`
- Lite file: `include/linux/spinlock.h`
- This step only changes: add Linux's default non-contended raw spinlock helper.

Mapping ledger:
- Functions: none
- Structs:
  - `raw_spinlock_t`: `linux2.6/include/linux/spinlock_types.h::raw_spinlock_t`, lite=`include/linux/spinlock_types.h`, placement=OK
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/spinlock.h`, lite=`include/linux/spinlock.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, macro name matches Linux.
- Placement: OK, macro is in `include/linux/spinlock.h`.
- Semantics: OK, Lite has no lockbreak or arch contended helper, so Linux's fallback returns 0.
- Flow/Lifetime: OK, query helper only.

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
- `git show -- include/linux/spinlock.h state.json Documentation/reviews/2026-04-30-stage3-raw-spin-is-contended.md`

Findings: none.
