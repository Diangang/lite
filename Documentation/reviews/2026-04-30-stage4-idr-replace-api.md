# Stage 4 Review: idr_replace API

Patch: `stage4-idr-replace-api`

## Linux Alignment Report

Change scope:
- Files: `lib/idr.c`, `include/linux/idr.h`, `include/linux/err.h`, `include/linux/errno.h`
- Directories: `lib`, `include/linux`
- Public surface: `idr_replace()`, `ERR_PTR()`/`PTR_ERR()`/`IS_ERR()` helpers, `ENOENT`

Reference-first evidence:
- Linux files: `linux2.6/lib/idr.c`, `linux2.6/include/linux/idr.h`, `linux2.6/include/linux/err.h`, `linux2.6/include/uapi/asm-generic/errno-base.h`
- Linux symbols: `idr_replace`, `ERR_PTR`, `PTR_ERR`, `IS_ERR`, `IS_ERR_OR_NULL`, `ERR_CAST`, `PTR_ERR_OR_ZERO`, `ENOENT`
- Lite files: `lib/idr.c`, `include/linux/idr.h`, `include/linux/err.h`, `include/linux/errno.h`
- This step only changes: add Linux's IDR replace helper and the Linux error-pointer return helpers it requires.

Mapping ledger:
- Functions/macros:
  - `idr_replace`: `linux2.6/lib/idr.c::idr_replace`, lite=`lib/idr.c`, placement=OK
  - `ERR_PTR`: `linux2.6/include/linux/err.h::ERR_PTR`, lite=`include/linux/err.h`, placement=OK
  - `PTR_ERR`: `linux2.6/include/linux/err.h::PTR_ERR`, lite=`include/linux/err.h`, placement=OK
  - `IS_ERR`: `linux2.6/include/linux/err.h::IS_ERR`, lite=`include/linux/err.h`, placement=OK
  - `IS_ERR_OR_NULL`: `linux2.6/include/linux/err.h::IS_ERR_OR_NULL`, lite=`include/linux/err.h`, placement=OK
  - `ERR_CAST`: `linux2.6/include/linux/err.h::ERR_CAST`, lite=`include/linux/err.h`, placement=OK
  - `PTR_ERR_OR_ZERO`: `linux2.6/include/linux/err.h::PTR_ERR_OR_ZERO`, lite=`include/linux/err.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/lib/idr.c` -> `lib/idr.c`, placement=OK
  - `linux2.6/include/linux/idr.h` -> `include/linux/idr.h`, placement=OK
  - `linux2.6/include/linux/err.h` -> `include/linux/err.h`, placement=OK
  - `linux2.6/include/uapi/asm-generic/errno-base.h` -> `include/linux/errno.h`, placement=DIFF for Lite's consolidated errno header
- Directories: `linux2.6/lib` -> `lib`, `linux2.6/include/linux` -> `include/linux`
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK for IDR and err helpers; ENOENT follows Lite's existing consolidated `include/linux/errno.h` errno placement.
- Semantics: OK, returns `ERR_PTR(-EINVAL)` for invalid id/input, `ERR_PTR(-ENOENT)` for missing id, otherwise replaces and returns old pointer.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: initial run terminated at Fork Blast after an NVMe read timeout in the mount path; immediate rerun passed.
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/err.h include/linux/errno.h include/linux/idr.h lib/idr.c state.json Documentation/reviews/2026-04-30-stage4-idr-replace-api.md`

Findings: none.
