# Stage 4 Review: radix_tree_deref_slot helper

Patch: `stage4-radix-tree-deref-slot-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/radix-tree.h`
- Directories: `include/linux`
- Public surface: `radix_tree_deref_slot()`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/radix-tree.h`
- Linux symbol: `radix_tree_deref_slot`
- Lite file: `include/linux/radix-tree.h`
- This step only changes: add Linux's same-name slot dereference helper for Lite's non-RCU radix-tree subset.

Mapping ledger:
- Functions:
  - `radix_tree_deref_slot`: `linux2.6/include/linux/radix-tree.h::radix_tree_deref_slot`, lite=`include/linux/radix-tree.h`, placement=OK
- Structs: none changed
- Globals/statics: none
- Files:
  - `linux2.6/include/linux/radix-tree.h`, lite=`include/linux/radix-tree.h`, placement=OK
- Directories:
  - `linux2.6/include/linux`, lite=`include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK, `radix_tree_deref_slot` matches Linux.
- Placement: OK.
- Semantics: OK for Lite subset; Lite has no RCU radix-tree indirection, so the helper directly loads `*pslot`.
- Flow/Lifetime: OK, header helper only.

## Validation

Commands:
- `make -j4`
- `make smoke-128`
- `make smoke-512`

Result:
- `make -j4`: passed
- `make smoke-128`: first run terminated during Fork Blast without an assertion failure; immediate rerun passed
- `make smoke-512`: passed

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/radix-tree.h state.json Documentation/reviews/2026-04-30-stage4-radix-tree-deref-slot-helper.md`

Findings: none.
