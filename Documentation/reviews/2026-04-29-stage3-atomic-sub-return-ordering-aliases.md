# Review: stage3-atomic-sub-return-ordering-aliases

Final commit: see `git log -1`.
Pre-review commit: fe55c4b0017a34d52a15f753cc33c8ab6d841c6e

## Scope

- `include/linux/atomic.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/atomic.h::atomic_sub_return_relaxed`
- Linux reference: `linux2.6/include/linux/atomic.h::atomic_sub_return_acquire`
- Linux reference: `linux2.6/include/linux/atomic.h::atomic_sub_return_release`
- Lite target: `include/linux/atomic.h::atomic_sub_return_relaxed`
- Lite target: `include/linux/atomic.h::atomic_sub_return_acquire`
- Lite target: `include/linux/atomic.h::atomic_sub_return_release`
- Single difference: Lite now provides Linux's ordered alias names for `atomic_sub_return()`.

Linux 2.6 maps the relaxed, acquire, and release variants to the fully ordered
`atomic_sub_return()` when no relaxed primitive is supplied by the architecture.
Lite now provides the same alias surface over its existing x86 atomic helper.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/atomic.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; first run timed out after an NVMe read timeout.
- `make smoke-512`: passed

## Findings

None.
