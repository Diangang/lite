# Stage 4 Review: kmem_cache_zalloc API

Patch: `stage4-slab-kmem-cache-zalloc-api`

## Linux Alignment Report

Change scope:
- Files: `include/linux/slab.h`
- Directories: `include/linux`
- Public surface: `kmem_cache_zalloc()`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/slab.h`
- Linux symbol: `kmem_cache_zalloc`
- Lite file: `include/linux/slab.h`
- This step only changes: add Linux's zero-fill cache allocation helper API.

Mapping ledger:
- Functions/macros:
  - `kmem_cache_zalloc`: `linux2.6/include/linux/slab.h::kmem_cache_zalloc`, lite=`include/linux/slab.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/slab.h` -> `include/linux/slab.h`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, returns kmem_cache_alloc-backed memory zero-filled to the actual object size; `gfp_t` is accepted as a no-op until Lite's kmem_cache_alloc gains Linux's gfp parameter.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/slab.h state.json Documentation/reviews/2026-04-30-stage4-slab-kmem-cache-zalloc-api.md`

Findings: none.
