# Review: stage3-reinit-completion-helper

Final commit: see `git log -1`.
Pre-review commit: ba6dad799fb9bbbd7f3bc3651cf857672db890f8

## Scope

- `include/linux/completion.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/completion.h::reinit_completion`
- Lite target: `include/linux/completion.h::reinit_completion`
- Single difference: Lite now provides Linux's `reinit_completion()` helper.

Linux 2.6 reinitializes a completion for reuse by clearing `done` without
reinitializing the embedded waitqueue. Lite now exposes the same helper next to
`init_completion()`, keeping the existing Lite null-pointer guard style while
preserving the Linux operation on valid completion objects.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/completion.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; an earlier run timed out near the NVMe raw test.
- `make smoke-512`: passed

## Findings

None.
