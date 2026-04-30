# Stage 4 Review: radix-tree preload API

Patch: `stage4-radix-tree-preload-api`

## Linux Alignment Report

Change scope:
- Files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- Directories: `lib`, `include/linux`
- Public surface: `radix_tree_preload()`, `radix_tree_maybe_preload()`, `radix_tree_preload_end()`

Reference-first evidence:
- Linux files: `linux2.6/lib/radix-tree.c`, `linux2.6/include/linux/radix-tree.h`
- Linux symbols: `radix_tree_preload`, `radix_tree_maybe_preload`, `radix_tree_preload_end`
- Lite files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- This step only changes: add Linux's radix-tree preload API names as a Lite subset over the existing on-demand allocation path.

Mapping ledger:
- Functions/macros:
  - `radix_tree_preload`: `linux2.6/lib/radix-tree.c::radix_tree_preload`, lite=`lib/radix-tree.c`, placement=OK
  - `radix_tree_maybe_preload`: `linux2.6/lib/radix-tree.c::radix_tree_maybe_preload`, lite=`lib/radix-tree.c`, placement=OK
  - `radix_tree_preload_end`: `linux2.6/include/linux/radix-tree.h::radix_tree_preload_end`, lite=`include/linux/radix-tree.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files:
  - `linux2.6/lib/radix-tree.c` -> `lib/radix-tree.c`, placement=OK
  - `linux2.6/include/linux/radix-tree.h` -> `include/linux/radix-tree.h`, placement=OK
- Directories: `linux2.6/lib` -> `lib`, `linux2.6/include/linux` -> `include/linux`
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: DIFF -> Lite does not maintain Linux's per-cpu preloaded node pool or preempt-disable section; the functions return success because Lite radix-tree insertions allocate nodes on demand with the root GFP mask.
- Flow/Lifetime: OK

If DIFF:
- Why: Lite currently has no preempt/per-cpu radix-tree preload machinery, and adding that would expand beyond this function-level API alignment step.
- Impact: Callers can use the Linux preload API names, but the Lite subset does not provide Linux's atomic-allocation guarantee for subsequent insertions.
- Plan: Keep this as an explicit subset until a later synchronization/per-cpu allocator stage can model the full Linux preload lifecycle.

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/radix-tree.h lib/radix-tree.c state.json Documentation/reviews/2026-04-30-stage4-radix-tree-preload-api.md`

Findings: none.
