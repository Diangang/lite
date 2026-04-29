# Review: stage3-wait-bit-action-type

Final commit: see `git log -1`.
Pre-review commit: 8a1021dcbced4086a06cd056191dad76f6ca715c

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::wait_bit_action_f`
- Lite target: `include/linux/wait.h::wait_bit_action_f`
- Single difference: Lite now provides Linux's wait-bit action callback type.

Linux 2.6 defines `wait_bit_action_f` as the callback function type used by
wait-bit helper declarations. Lite now provides the same typedef without adding
new wait-bit behavior or callers.

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
