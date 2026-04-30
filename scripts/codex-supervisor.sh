#!/bin/sh
set -eu

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$repo_root"

state_file="${CODEX_STATE_FILE:-state.json}"
prompt_file="${CODEX_PROMPT_FILE:-Documentation/codex-supervisor-prompt.md}"
codex_bin="${CODEX_BIN:-codex}"
log_dir="${CODEX_LOG_DIR:-logs/agent-runs}"
max_rounds="${CODEX_MAX_ROUNDS:-0}"
round_timeout="${CODEX_ROUND_TIMEOUT:-0}"
sleep_after_round="${CODEX_SLEEP_AFTER_ROUND:-1}"
dry_run="${CODEX_DRY_RUN:-0}"

if ! command -v jq >/dev/null 2>&1; then
    echo "codex-supervisor: jq is required" >&2
    exit 2
fi

if [ ! -f "$state_file" ]; then
    echo "codex-supervisor: missing state file: $state_file" >&2
    exit 2
fi

if [ ! -f "$prompt_file" ]; then
    echo "codex-supervisor: missing prompt file: $prompt_file" >&2
    exit 2
fi

mkdir -p "$log_dir"

state_get() {
    jq -r "$1" "$state_file"
}

state_update() {
    tmp="$(mktemp)"
    jq "$@" "$state_file" >"$tmp"
    mv "$tmp" "$state_file"
}

if [ "$dry_run" = "1" ]; then
    status="$(jq -r '.run_control.status // "running"' "$state_file")"
    stop_condition="$(jq -r '.run_control.stop_condition // ""' "$state_file")"
    echo "codex-supervisor: dry run ok: status=$status stop_condition=$stop_condition prompt=$prompt_file"
    exit 0
fi

round=0
while :; do
    round=$(( round + 1 ))
    if [ "$max_rounds" -gt 0 ] && [ "$round" -gt "$max_rounds" ]; then
        state_update '.run_control.status = "needs_user" |
            .run_control.stop_condition = "budget_exhausted" |
            .run_control.last_exit = "round_budget_exhausted"'
        echo "codex-supervisor: max rounds reached: $max_rounds" >&2
        exit 0
    fi

    status="$(state_get '.run_control.status // "running"')"
    stop_condition="$(state_get '.run_control.stop_condition // ""')"
    if [ "$status" = "done" ] || [ "$status" = "needs_user" ] || [ -n "$stop_condition" ]; then
        echo "codex-supervisor: stopping before round $round: status=$status stop_condition=$stop_condition" >&2
        exit 0
    fi

    stamp="$(date +%Y%m%d-%H%M%S)"
    log_file="$log_dir/round-${stamp}-${round}.log"
    heartbeat="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

    state_update --arg round "$round" --arg log "$log_file" --arg heartbeat "$heartbeat" '
        .run_control.status = "running" |
        .run_control.active_step = "codex round running" |
        .run_control.last_round = ($round | tonumber) |
        .run_control.last_log = $log |
        .run_control.heartbeat = $heartbeat |
        .run_control.last_exit = null'

    prompt="$(cat "$prompt_file")"
    exit_code=0
    if [ "$round_timeout" -gt 0 ] && command -v timeout >/dev/null 2>&1; then
        timeout "$round_timeout" "$codex_bin" "$prompt" >"$log_file" 2>&1 || exit_code="$?"
    else
        "$codex_bin" "$prompt" >"$log_file" 2>&1 || exit_code="$?"
    fi

    if [ "$exit_code" -eq 124 ]; then
        heartbeat="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        state_update --arg log "$log_file" --arg heartbeat "$heartbeat" '
            .run_control.status = "running" |
            .run_control.last_exit = "timeout" |
            .run_control.last_log = $log |
            .run_control.heartbeat = $heartbeat'
    elif [ "$exit_code" -ne 0 ]; then
        heartbeat="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        state_update --arg code "$exit_code" --arg log "$log_file" --arg heartbeat "$heartbeat" '
            .run_control.status = "needs_user" |
            .run_control.stop_condition = "codex_process_failed" |
            .run_control.last_exit = ("exit_" + $code) |
            .run_control.last_log = $log |
            .run_control.heartbeat = $heartbeat'
    else
        heartbeat="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        state_update --arg log "$log_file" --arg heartbeat "$heartbeat" '
            .run_control.last_exit = "clean_process_exit" |
            .run_control.last_log = $log |
            .run_control.heartbeat = $heartbeat'
    fi

    status="$(state_get '.run_control.status // "running"')"
    stop_condition="$(state_get '.run_control.stop_condition // ""')"
    if [ "$status" = "done" ] || [ "$status" = "needs_user" ] || [ -n "$stop_condition" ]; then
        echo "codex-supervisor: stopping after round $round: status=$status stop_condition=$stop_condition" >&2
        exit 0
    fi

    sleep "$sleep_after_round"
done
