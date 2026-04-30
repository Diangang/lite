# Stage 4 Review: kmemdup API

Patch: `stage4-mm-kmemdup-api`

## Linux Alignment Report

Change scope:
- Files: `mm/util.c`, `include/linux/string.h`, `Makefile`
- Directories: `mm`, `include/linux`
- Public surface: `kmemdup()`

Reference-first evidence:
- Linux files: `linux2.6/mm/util.c`, `linux2.6/include/linux/string.h`
- Linux symbol: `kmemdup`
- Lite files: `mm/util.c`, `include/linux/string.h`
- This step only changes: add Linux's memory-duplication helper API and build placement.

Mapping ledger:
- Functions/macros:
  - `kmemdup`: `linux2.6/mm/util.c::kmemdup`, lite=`mm/util.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/mm/util.c` -> `mm/util.c`, placement=OK
  - `linux2.6/include/linux/string.h` -> `include/linux/string.h`, placement=OK
- Directories:
  - `linux2.6/mm` -> `mm`, placement=OK
  - `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, allocates `len` bytes and copies from `src`; `gfp_t` is accepted as a no-op until Lite's `kmalloc` gains Linux's gfp parameter.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: initial run failed during `/mnt_nvme` mount with `nvme: io read failed lba=2 nlb=2 len=1024 err=-110`; immediate rerun passed.
- `make smoke-512`: initial run failed during `/mnt_nvme` mount with `nvme: io read failed lba=8 nlb=2 len=1024 err=-110` and `vfs_get_sb_single: fill_super failed`; immediate rerun passed.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- Makefile include/linux/string.h mm/util.c state.json Documentation/reviews/2026-04-30-stage4-mm-kmemdup-api.md`

Findings: none.
