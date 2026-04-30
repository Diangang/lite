# Codex Supervisor

This repository supports long-running Codex work by treating Codex as a
restartable worker, not as an always-live process.

## Model

`scripts/codex-supervisor.sh` is the long-running process. Each loop starts one
Codex round with `Documentation/codex-supervisor-prompt.md`, waits for Codex to
return or time out, checks `state.json`, then either starts another round or
stops on a machine-readable condition.

This means a Codex final response, context compaction, terminal disconnect, or
tool timeout does not automatically stop the overall task. The supervisor keeps
running unless `state.json` says it should stop.

## Files

- `scripts/codex-supervisor.sh`: external loop.
- `Documentation/codex-supervisor-prompt.md`: fixed prompt passed to each Codex
  round.
- `state.json`: durable task state and stop policy.
- `logs/agent-runs/`: per-round Codex stdout/stderr logs.
- `Documentation/reviews/`: per-patch review and Linux alignment records.
- git commits: durable patch checkpoints.

## Running

Basic run:

```sh
scripts/codex-supervisor.sh
```

Useful environment variables:

```sh
CODEX_BIN=codex
CODEX_ARGS=exec
CODEX_STATE_FILE=state.json
CODEX_PROMPT_FILE=Documentation/codex-supervisor-prompt.md
CODEX_LOG_DIR=logs/agent-runs
CODEX_MAX_ROUNDS=0
CODEX_ROUND_TIMEOUT=0
CODEX_SLEEP_AFTER_ROUND=1
CODEX_STREAM_LOG=1
```

Examples:

```sh
CODEX_MAX_ROUNDS=10 CODEX_ROUND_TIMEOUT=3600 scripts/codex-supervisor.sh
```

`CODEX_MAX_ROUNDS=0` means unlimited rounds. `CODEX_ROUND_TIMEOUT=0` disables
the external timeout. If `timeout(1)` is unavailable, the script runs without a
timeout even when `CODEX_ROUND_TIMEOUT` is set. `CODEX_ARGS=exec` runs Codex in
non-interactive mode so supervisor logging can redirect stdout/stderr.
`CODEX_STREAM_LOG=1` mirrors Codex output to the terminal while also writing the
round log file. Set `CODEX_STREAM_LOG=0` for file-only logging.

## Stop Contract

The supervisor stops only when:

- `.run_control.status` is `done` or `needs_user`;
- `.run_control.stop_condition` is non-empty;
- the supervisor itself reaches `CODEX_MAX_ROUNDS`;
- Codex exits with a non-timeout process error.

Timeout exit code `124` is treated as recoverable: the supervisor records
`last_exit=timeout` and starts the next round unless Codex also wrote a stop
condition.

Allowed task stop conditions are:

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

Clean checkpoints are not stop conditions. A clean commit, successful smoke run,
clean review, stage transition, and clean worktree are recovery points only.

## Recovery Rules

At the beginning of each Codex round:

1. Read `state.json`.
2. Check `git status --short`.
3. If the worktree is dirty, finish or classify the in-progress patch before
   selecting a new patch.
4. If the worktree is clean and no stop condition exists, continue to the next
   roadmap patch.

For code changes, git commit is the durable patch boundary. For failures,
`run_control.stop_condition` is the durable stop boundary.
