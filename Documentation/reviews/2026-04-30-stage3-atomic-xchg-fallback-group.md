# Stage 3 Review: atomic xchg fallback group

Patch: `stage3-atomic-xchg-fallback-group`

## Linux Alignment Report

Change scope:
- Files: `include/linux/atomic.h`
- Directories: `include/linux`
- Public surface: `atomic_xchg_relaxed`, `atomic_xchg_acquire`, `atomic_xchg_release`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/atomic.h`
- Linux symbols: `atomic_xchg_relaxed`, `atomic_xchg_acquire`, `atomic_xchg_release`
- Lite file: `include/linux/atomic.h`
- This step only changes: place xchg acquire/release fallbacks inside the same relaxed fallback guard as Linux.

Mapping ledger:
- Functions: none
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/atomic.h`, lite=`include/linux/atomic.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, macro names match Linux.
- Placement: OK.
- Semantics: OK, fallback aliases still resolve to `atomic_xchg`.
- Flow/Lifetime: OK, preprocessor fallback shape only.

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
- `git show -- include/linux/atomic.h state.json Documentation/reviews/2026-04-30-stage3-atomic-xchg-fallback-group.md`

Findings: none.
