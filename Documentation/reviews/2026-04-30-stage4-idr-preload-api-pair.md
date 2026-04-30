# Stage 4 Review: IDR preload API pair

Patch: `stage4-idr-preload-api-pair`

## Linux Alignment Report

Change scope:
- Files: `lib/idr.c`, `include/linux/idr.h`
- Directories: `lib`, `include/linux`
- Public surface: `idr_preload()`, `idr_preload_end()`

Reference-first evidence:
- Linux file: `linux2.6/lib/idr.c`
- Linux symbols: `idr_preload`, `idr_preload_end`
- Linux header: `linux2.6/include/linux/idr.h`
- Lite files: `lib/idr.c`, `include/linux/idr.h`
- This step only changes: add the Linux preload API pair as a no-op Lite subset.

Mapping ledger:
- Functions:
  - `idr_preload`: `linux2.6/lib/idr.c::idr_preload`, lite=`lib/idr.c`, placement=OK
  - `idr_preload_end`: `linux2.6/include/linux/idr.h::idr_preload_end`, lite=`include/linux/idr.h`, placement=OK
- Structs:
  - `struct idr`: `linux2.6/include/linux/idr.h::struct idr`, lite=`include/linux/idr.h`, placement=OK for Lite subset
- Globals/statics:
  - Linux per-cpu preload cache: not added; Lite allocates IDR nodes directly
- Files:
  - `linux2.6/lib/idr.c`, lite=`lib/idr.c`, placement=OK
  - `linux2.6/include/linux/idr.h`, lite=`include/linux/idr.h`, placement=OK
- Directories:
  - `linux2.6/lib`, lite=`lib`, placement=OK
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none for the public API pair

Consistency:
- Naming: OK, API names match Linux.
- Placement: OK.
- Semantics: OK for Lite subset; no preload guarantee is added because Lite has no per-cpu IDR layer cache or preempt API.
- Flow/Lifetime: OK, no cached layer lifetime is introduced.

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
- `git show -- lib/idr.c include/linux/idr.h state.json Documentation/reviews/2026-04-30-stage4-idr-preload-api-pair.md`

Findings: none.
