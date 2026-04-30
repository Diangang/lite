# Stage 4 Review: __get_dma_pages helper

Patch: `stage4-gfp-get-dma-pages-helper`

## Linux Alignment Report

Change scope:
- Files: `include/linux/gfp.h`
- Directories: `include/linux`
- Public surface: `__get_dma_pages()`

Reference-first evidence:
- Linux file: `linux2.6/include/linux/gfp.h`
- Linux symbol: `__get_dma_pages`
- Lite file: `include/linux/gfp.h`
- This step only changes: add Linux's DMA-zone wrapper over `__get_free_pages(gfp | GFP_DMA, order)`.

Mapping ledger:
- Functions/macros:
  - `__get_dma_pages`: `linux2.6/include/linux/gfp.h::__get_dma_pages`, lite=`include/linux/gfp.h`, placement=OK
- Structs: none
- Globals/statics: none
- Files: `linux2.6/include/linux/gfp.h` -> `include/linux/gfp.h`, placement=OK
- Directories: `linux2.6/include/linux` -> `include/linux`, placement=OK
- NO_DIRECT_LINUX_MATCH: none

Consistency:
- Naming: OK
- Placement: OK
- Semantics: OK, direct wrapper over existing `GFP_DMA` zonelist selection.
- Flow/Lifetime: OK

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: first run terminated at `NVMe MinixFS Mount + R/W` without an assertion failure; immediate rerun passed.

## Review

Commands:
- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/gfp.h state.json Documentation/reviews/2026-04-30-stage4-gfp-get-dma-pages-helper.md`

Findings: none.
