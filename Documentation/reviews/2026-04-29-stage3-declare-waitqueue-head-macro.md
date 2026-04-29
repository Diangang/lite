# Review: stage3-declare-waitqueue-head-macro

Final commit: see `git log -1`.
Pre-review commit: 1b6025d1cafdcfa1b8f04bfcfd4cac0e178c0806

## Scope

- `include/linux/wait.h`
- `state.json`

## Linux Alignment

- Linux reference: `linux2.6/include/linux/wait.h::__WAIT_QUEUE_HEAD_INITIALIZER`
- Linux reference: `linux2.6/include/linux/wait.h::DECLARE_WAIT_QUEUE_HEAD`
- Lite target: `include/linux/wait.h`
- Single difference: Lite now provides Linux's waitqueue head declaration initializer macros.

Linux 2.6 initializes a static waitqueue head with an unlocked spinlock and a
self-linked `task_list`. Lite already has matching spinlock and list initializer
building blocks, so the new macros mirror the Linux declaration form without
changing dynamic `init_waitqueue_head()` users.

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
