# Stage 4 Review: radix_tree_lookup_slot API

Patch: `stage4-radix-tree-lookup-slot-api`

## Linux Alignment Report

Change scope:
- Files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- Directories: `lib`, `include/linux`
- Public surface: `radix_tree_lookup_slot()`

Reference-first evidence:
- Linux file: `linux2.6/lib/radix-tree.c`
- Linux symbol: `radix_tree_lookup_slot`
- Linux header: `linux2.6/include/linux/radix-tree.h`
- Lite files: `lib/radix-tree.c`, `include/linux/radix-tree.h`
- This step only changes: add Linux's same-name slot lookup API for the existing Lite radix-tree subset.

Mapping ledger:
- Functions:
  - `radix_tree_lookup_slot`: `linux2.6/lib/radix-tree.c::radix_tree_lookup_slot`, lite=`lib/radix-tree.c`, placement=OK
- Structs:
  - `struct radix_tree_root`: `linux2.6/include/linux/radix-tree.h::struct radix_tree_root`, lite=`include/linux/radix-tree.h`, placement=OK
  - `struct radix_tree_node`: `linux2.6/include/linux/radix-tree.h::struct radix_tree_node`, lite=`include/linux/radix-tree.h`, placement=OK for Lite subset
- Globals/statics: none
- Files:
  - `linux2.6/lib/radix-tree.c`, lite=`lib/radix-tree.c`, placement=OK
  - `linux2.6/include/linux/radix-tree.h`, lite=`include/linux/radix-tree.h`, placement=OK
- Directories:
  - `linux2.6/lib`, lite=`lib`, placement=OK
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, `radix_tree_lookup_slot` matches Linux.
- Placement: OK.
- Semantics: OK for Lite subset, returning the matching slot address or `NULL` if no item exists.
- Flow/Lifetime: OK, no radix-tree allocation or deletion flow changes.

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
- `git show -- lib/radix-tree.c include/linux/radix-tree.h state.json Documentation/reviews/2026-04-30-stage4-radix-tree-lookup-slot-api.md`

Findings: none.
