# Review: stage3-declare-completion-onstack-macro

Final commit: see `git log -1`.
Pre-review commit: 3c94b32cd44ac47a9f838a25374480e848602776

## Scope

- `include/linux/completion.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/completion.h::COMPLETION_INITIALIZER_ONSTACK`
- Linux reference: `linux2.6/include/linux/completion.h::DECLARE_COMPLETION_ONSTACK`
- Lite target: `include/linux/completion.h`
- Single difference: Lite now provides Linux's on-stack completion declaration macros.

Linux 2.6 routes on-stack completion declarations through a non-constant
initializer when lockdep is enabled, and otherwise aliases them to the static
completion declaration form. Lite now carries the same macro interface and
conditional shape; its normal non-lockdep build aliases to `DECLARE_COMPLETION()`.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/completion.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe mount/read path.

## Findings

None.
