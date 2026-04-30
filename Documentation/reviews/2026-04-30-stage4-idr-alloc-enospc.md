# Stage 4 Review: idr_alloc exhausted range return

Patch: `stage4-idr-alloc-enospc`

## Linux Alignment Report

Change scope:
- Files: `lib/idr.c`, `include/linux/errno.h`
- Directories: `lib`, `include/linux`
- Public surface: `idr_alloc()` return code for exhausted ID range; `ENOSPC`

Reference-first evidence:
- Linux file: `linux2.6/lib/idr.c`
- Linux symbol: `idr_alloc`
- Linux errno reference: `linux2.6/include/uapi/asm-generic/errno-base.h`
- Lite files: `lib/idr.c`, `include/linux/errno.h`
- This step only changes: return `-ENOSPC` when `idr_alloc()` exhausts the requested range while preserving `-ENOMEM` for allocation failure.

Mapping ledger:
- Functions:
  - `idr_alloc`: `linux2.6/lib/idr.c::idr_alloc`, lite=`lib/idr.c`, placement=OK
- Structs:
  - `struct idr`: `linux2.6/include/linux/idr.h::struct idr`, lite=`include/linux/idr.h`, placement=OK for Lite subset
- Globals/statics: none
- Files:
  - `linux2.6/lib/idr.c`, lite=`lib/idr.c`, placement=OK
  - `linux2.6/include/uapi/asm-generic/errno-base.h`, lite=`include/linux/errno.h`, placement=DIFF for Lite's collapsed errno subset
- Directories:
  - `linux2.6/lib`, lite=`lib`, placement=OK
  - `linux2.6/include`, lite=`include`, placement=OK for Lite's collapsed headers
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, `ENOSPC` and `idr_alloc` match Linux.
- Placement: OK for `idr_alloc`; errno placement follows Lite's existing collapsed `include/linux/errno.h`.
- Semantics: OK, exhausted ID range now returns `-ENOSPC`; allocation failure remains `-ENOMEM`.
- Flow/Lifetime: OK, no allocation or object lifetime flow changes.

If DIFF:
- Why: Lite keeps errno constants in a minimal collapsed header instead of the Linux UAPI errno split.
- Impact: This patch adds only Linux's `ENOSPC` numeric value needed by `idr_alloc`.
- Plan: Keep broader errno header placement out of this patch.

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
- `git show -- lib/idr.c include/linux/errno.h state.json Documentation/reviews/2026-04-30-stage4-idr-alloc-enospc.md`

Findings: none.
