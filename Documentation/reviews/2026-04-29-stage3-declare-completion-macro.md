# Review: stage3-declare-completion-macro

Final commit: see `git log -1`.
Pre-review commit: fb38dcd8988db57fdf42b659e89691bda3870b3e

## Scope

- `include/linux/completion.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/completion.h::COMPLETION_INITIALIZER`
- Linux reference: `linux2.6/include/linux/completion.h::DECLARE_COMPLETION`
- Lite target: `include/linux/completion.h`
- Single difference: Lite now provides Linux's static completion declaration initializer macros.

Linux 2.6 builds a static completion initializer from a zero `done` count and
the waitqueue head initializer for the embedded waitqueue. Lite now exposes the
same declaration form using its existing completion fields and newly aligned
waitqueue head initializer.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/completion.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed

## Findings

None.
