# Review: stage3-completion-done-helper

Final commit: see `git log -1`.
Pre-review commit: c2f8247579419aa773bc1453bfe432091da0dba1

## Scope

- `include/linux/completion.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/kernel/sched/completion.c::completion_done`
- Linux reference: `linux2.6/include/linux/completion.h::completion_done`
- Lite target: `include/linux/completion.h::completion_done`
- Single difference: Lite now provides Linux's `completion_done()` helper.

Linux 2.6 returns false when `done` is clear, then uses `smp_rmb()` and
`spin_unlock_wait()` before reporting the completion as done. Lite now follows
the same completion query shape and ordering with the spin unlock-wait helper
added in the previous patch. Lite does not yet expose `READ_ONCE()`, so this
patch keeps the direct `done` load to avoid combining a separate compiler API
alignment into this change.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/completion.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount/write test.

## Findings

None.
