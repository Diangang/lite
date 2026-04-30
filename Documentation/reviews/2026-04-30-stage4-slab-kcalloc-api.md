# Stage 4 Review: kcalloc API

Patch: `stage4-slab-kcalloc-api`

## Linux Alignment Report

Change scope:
- Files: `include/linux/slab.h`
- Directories: `include/linux`
- Public surface: `kcalloc()`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/slab.h`
- Linux symbol: `kcalloc`
- Lite file: `include/linux/slab.h`
- This step only changes: add Linux's zeroed array allocation helper API.

Mapping ledger:
- Functions/macros:
  - `kcalloc`: `linux2.6/include/linux/slab.h::kcalloc`, lite=`include/linux/slab.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/slab.h` -> `include/linux/slab.h`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, delegates overflow checking to `kmalloc_array()` and zeroes the requested array byte range; this preserves `kcalloc()` zero-fill semantics while Lite's `kmalloc_array()` still accepts `gfp_t` as a no-op.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: initial run failed during `/mnt_nvme` mount with `nvme: io read failed lba=2 nlb=2 len=1024 err=-110`; immediate rerun passed.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/slab.h state.json Documentation/reviews/2026-04-30-stage4-slab-kcalloc-api.md`

Findings: none.
