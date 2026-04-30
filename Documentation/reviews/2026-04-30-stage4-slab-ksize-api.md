# Stage 4 Review: ksize API

Patch: `stage4-slab-ksize-api`

## Linux Alignment Report

Change scope:
- Files: `include/linux/slab.h`, `mm/slab.c`
- Directories: `include/linux`, `mm`
- Public surface: `ksize()`

Reference-first evidence:
- Linux files: `linux2.6/include/linux/slab.h`, `linux2.6/mm/slab.c`
- Linux symbol: `ksize`
- Lite files: `include/linux/slab.h`, `mm/slab.c`
- This step only changes: add Linux's API for returning the actual allocated object size.

Mapping ledger:
- Functions:
  - `ksize`: `linux2.6/mm/slab.c::ksize`, lite=`mm/slab.c`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/slab.h` -> `include/linux/slab.h`, placement=OK
  - `linux2.6/mm/slab.c` -> `mm/slab.c`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, `linux2.6/mm` -> `mm`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK for valid kmalloc/kmem_cache_alloc objects; returns the slab cache object size or large allocation usable size.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: first run terminated at `NVMe MinixFS Mount + R/W` without an assertion failure; immediate rerun passed.
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/slab.h mm/slab.c state.json Documentation/reviews/2026-04-30-stage4-slab-ksize-api.md`

Findings: none.
