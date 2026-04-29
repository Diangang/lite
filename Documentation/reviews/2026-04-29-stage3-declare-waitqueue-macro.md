# Review: stage3-declare-waitqueue-macro

Final commit: see `git log -1`.
Pre-review commit: d91cbfd64d20347c4379c4edb671c355d3ab4187

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__WAITQUEUE_INITIALIZER`
- Linux reference: `linux2.6/include/linux/wait.h::DECLARE_WAITQUEUE`
- Lite target: `include/linux/wait.h`
- Single difference: Lite now provides Linux's waitqueue entry declaration initializer macros.

Linux 2.6 declares `__WAITQUEUE_INITIALIZER()` and `DECLARE_WAITQUEUE()` next
to the waitqueue data type definitions. Lite now exposes the same entry
initializer shape, including `default_wake_function` and an unlinked
`task_list`, without changing current runtime waitqueue users.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed after rerun; earlier runs timed out near the NVMe raw and NVMe MinixFS tests.
- `make smoke-512`: passed

## Findings

None.
