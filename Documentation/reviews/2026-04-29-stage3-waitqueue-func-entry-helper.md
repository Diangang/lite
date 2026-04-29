# Review: stage3-waitqueue-func-entry-helper

Final commit: see `git log -1`.
Pre-review commit: dbc27bce02969cf6ab77b6de32b9074fd4ecaccf

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::init_waitqueue_func_entry`
- Lite target: `include/linux/wait.h::init_waitqueue_func_entry`
- Single difference: Lite now provides Linux's `init_waitqueue_func_entry()` inline helper.

Linux 2.6 initializes a waitqueue entry for a custom wake function by clearing
the flags, setting `private` to `NULL`, and storing the function pointer. Lite
now exposes the same helper over its existing waitqueue entry fields without
changing existing `init_waitqueue_entry()` callers.

## Review Commands

- `git show --stat --oneline --decorate HEAD`
- `git show --check --oneline HEAD`
- `git show -- include/linux/wait.h state.json`

## Validation

- `make -j4`: passed
- `make smoke-128`: passed
- `make smoke-512`: passed after rerun; an earlier run timed out near the NVMe MinixFS mount/write test.

## Findings

None.
