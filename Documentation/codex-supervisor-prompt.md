You are running under the repository's external Codex supervisor.

Read `state.json` first, then continue the task described by `state.json` and
`Documentation/Linux26-Roadmap.md`.

Required behavior for each supervisor round:

- Treat `state.json` and git status as the recovery boundary.
- If `run_control.stop_condition` is non-empty, report that condition and do
  not continue.
- If there are dirty files, inspect them first and either finish the in-progress
  patch or set a machine-readable stop condition.
- If the worktree is clean and no stop condition exists, select the next narrow
  in-scope patch from the roadmap.
- For Linux alignment work, use the linux-alignment skill, read Linux reference
  before editing, write a mapping ledger in the review record, keep one
  alignment difference per patch, validate, commit, and review.
- Update `run_control.heartbeat`, `run_control.phase`, `run_control.active_step`,
  and the existing validation/review fields at every meaningful transition.
- Do not stop merely because a patch is committed, validation passes, review is
  clean, a stage boundary is reached, the worktree is clean, context is compacted,
  or a response boundary is reached.
- If Codex would otherwise send a final answer without a real stop condition,
  continue by selecting the next patch.

Allowed machine-readable stop conditions:

- `validation_failure`
- `review_findings`
- `route_change_outside_roadmap`
- `skipping_required_tests`
- `new_non_linux_abstraction`
- `destructive_operation`
- `dirty_worktree_conflict`
- `no_safe_next_patch`
- `needs_human_decision`
- `budget_exhausted`
- `codex_process_failed`

When stopping, write:

```json
"run_control": {
  "status": "needs_user",
  "stop_condition": "<one of the allowed conditions>",
  "active_step": "<what was being attempted>",
  "last_error": "<short concrete reason>"
}
```

If no allowed stop condition is present, keep working.
