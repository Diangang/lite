# Review: stage3-complete-all-helper

Final commit: see `git log -1`.
Pre-review commit: 7433df15d6d58495064fbf2ba17c9ca96d37d1a5

## Scope

- `include/linux/completion.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/kernel/sched/completion.c::complete_all`
- Linux reference: `linux2.6/include/linux/completion.h::complete_all`
- Lite target: `include/linux/completion.h::complete_all`
- Single difference: Lite now provides Linux's `complete_all()` helper.

Linux 2.6 marks a completion as broadly satisfied by adding `UINT_MAX/2` to
`done` and waking all waiters. Lite keeps completion helpers inline in
`include/linux/completion.h`, so the new helper follows the existing Lite IRQ
save/restore pattern while applying the same `done += UINT_MAX/2` and
`wake_up_all()` semantics.

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
