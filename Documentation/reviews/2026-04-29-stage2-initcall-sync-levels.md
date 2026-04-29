# Review: stage2-initcall-sync-levels

Final commit: see `git log -1`.
Pre-review commit: d0f304820bcc802ce80951dd2098c83afa8bda90

## Scope

- `include/linux/init.h`
- `arch/x86/kernel/linker.ld`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/init.h::*_initcall_sync`
- Linux reference: Linux initcall section ordering for `.initcall<N>s.init`
- Lite target: `include/linux/init.h::*_initcall_sync`
- Lite target: `arch/x86/kernel/linker.ld::.initcall.init`
- Single difference: add synchronous initcall level macros and place their sections immediately after the corresponding non-sync level.

Linux defines `core_initcall_sync()` through `late_initcall_sync()` as separate
initcall subsections (`1s` through `7s`). Lite now preserves that call-site
vocabulary and linker order, without adding new initcall policy.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/init.h arch/x86/kernel/linker.ld state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
