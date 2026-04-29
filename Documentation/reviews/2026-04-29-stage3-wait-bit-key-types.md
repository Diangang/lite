# Review: stage3-wait-bit-key-types

Final commit: see `git log -1`.
Pre-review commit: db386e9dfa8dcde463946151fc77a4114e263cb1

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::wait_bit_key`
- Linux reference: `linux2.6/include/linux/wait.h::wait_bit_queue`
- Lite target: `include/linux/wait.h::wait_bit_key`
- Lite target: `include/linux/wait.h::wait_bit_queue`
- Single difference: Lite now provides Linux's wait-bit key data types.

Linux 2.6 defines `struct wait_bit_key`, `WAIT_ATOMIC_T_BIT_NR`, and
`struct wait_bit_queue` in `include/linux/wait.h` as the data foundation for
bit waitqueue helpers. Lite now provides the same type names and field layout
without adding new wait-bit behavior or callers.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
