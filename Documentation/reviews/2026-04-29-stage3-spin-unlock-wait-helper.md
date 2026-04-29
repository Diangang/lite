# Review: stage3-spin-unlock-wait-helper

Final commit: see `git log -1`.
Pre-review commit: 1a26022e3b90159fa0285d63d0e765d988934530

## Scope

- `include/linux/spinlock.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/spinlock.h::raw_spin_unlock_wait`
- Linux reference: `linux2.6/include/linux/spinlock.h::spin_unlock_wait`
- Lite target: `include/linux/spinlock.h`
- Single difference: Lite now provides Linux's spinlock unlock-wait helper.

Linux 2.6 exposes `spin_unlock_wait()` as the public helper and delegates to
the raw lock implementation. Lite now mirrors that split with
`raw_spin_unlock_wait()` polling the existing raw lock state and
`spin_unlock_wait()` delegating through the embedded raw lock.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/spinlock.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
