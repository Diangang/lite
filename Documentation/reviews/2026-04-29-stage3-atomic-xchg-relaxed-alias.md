# Review: stage3-atomic-xchg-relaxed-alias

Final commit: see `git log -1`.
Pre-review commit: 60bc9693b954504d7f21e40d22275647285fc8d7

## Scope

- `include/linux/atomic.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/atomic.h::atomic_xchg_relaxed`
- Lite target: `include/linux/atomic.h::atomic_xchg_relaxed`
- Single difference: Lite now provides Linux's relaxed alias name for `atomic_xchg()`.

Linux 2.6 maps `atomic_xchg_relaxed` to the fully ordered `atomic_xchg()` when
no relaxed primitive is supplied by the architecture. Lite now provides the same
alias over its existing x86 atomic helper.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/atomic.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
