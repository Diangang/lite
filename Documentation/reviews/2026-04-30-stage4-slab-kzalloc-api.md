# Stage 4 Review: kzalloc API

Patch: `stage4-slab-kzalloc-api`

## Linux Alignment Report

Change scope:
- Files: `include/linux/slab.h`
- Directories: `include/linux`
- Public surface: `kzalloc()`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/slab.h`
- Linux symbol: `kzalloc`
- Lite file: `include/linux/slab.h`
- This step only changes: add Linux's zero-fill kmalloc wrapper API.

Mapping ledger:
- Functions/macros:
  - `kzalloc`: `linux2.6/include/linux/slab.h::kzalloc`, lite=`include/linux/slab.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/slab.h` -> `include/linux/slab.h`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, returns kmalloc-backed memory zero-filled to the actual object size; `gfp_t` is accepted as a no-op until Lite's kmalloc gains Linux's gfp parameter.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/slab.h state.json Documentation/reviews/2026-04-30-stage4-slab-kzalloc-api.md`

Findings: none.
