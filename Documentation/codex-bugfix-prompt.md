You are running under the repository's external Codex supervisor in BUGFIX mode.

Read `state.json`, `git status --short`, and the latest log in
`run_control.last_log` first. The normal long-running workflow hit
`run_control.stop_condition`; your job in this round is to diagnose and fix that
specific blocker, validate it, update state, and then return control to the
normal workflow.

Bugfix rules:

- Do not select a new roadmap patch while `run_control.stop_condition` is set.
- Treat dirty files as the interrupted work that triggered this bugfix unless
  inspection proves they are unrelated user changes.
- Locate the root cause using local evidence: failing command output, git diff,
  current state, and relevant source.
- If the failure is storage, scheduler, interrupt, VFS, memory, or Linux
  alignment related, use the relevant skill before editing.
- Keep the bugfix as narrow as possible. If the bugfix is independent from the
  interrupted patch, commit it separately before resuming the interrupted patch.
- After a fix, rerun the failing command first, then any required validation for
  affected code. For Lite Linux alignment work that means `make -j4`,
  `make smoke-128`, and `make smoke-512` unless a real stop condition prevents
  it.
- If validation passes, clear the stop condition and set:

```json
"run_control": {
  "status": "running",
  "stop_condition": null,
  "last_error": null,
  "phase": "ready_for_next_patch or review_clean",
  "bugfix": {
    "status": "fixed"
  }
}
```

- If the bugfix cannot be completed safely, keep or set a machine-readable
  stop condition and explain the concrete blocker in `run_control.last_error`.

Hard stop conditions that bugfix mode must not bypass:

- `destructive_operation`
- `route_change_outside_roadmap`
- `new_non_linux_abstraction`
- `dirty_worktree_conflict`
- `no_safe_next_patch`
- `needs_human_decision`
- `budget_exhausted`

For recoverable conditions, keep working until either the condition is fixed or
the supervisor's bugfix attempt budget is exhausted.
