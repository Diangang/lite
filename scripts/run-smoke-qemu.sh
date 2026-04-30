#!/bin/sh
set -eu

if [ "$#" -ne 6 ]; then
    echo "usage: $0 <mem> <kernel> <initrd> <scsi_img> <nvme0_img> <nvme1_img>" >&2
    exit 2
fi

mem="$1"
kernel="$2"
initrd="$3"
scsi_img="$4"
nvme0_img="$5"
nvme1_img="$6"

smoke_timeout="${SMOKE_TIMEOUT:-60}"
input_delay="${SMOKE_INPUT_DELAY:-5}"

tmp="$(mktemp)"
scsi="$(mktemp)"
nv0="$(mktemp)"
nv1="$(mktemp)"
pidfile="$(mktemp)"

cleanup() {
    rm -f "$tmp" "$scsi" "$nv0" "$nv1"
    rm -f "$pidfile"
}
trap cleanup EXIT INT TERM HUP

cp "$scsi_img" "$scsi"
cp "$nvme0_img" "$nv0"
cp "$nvme1_img" "$nv1"

# Run QEMU under a pipeline (for stdin injection), but also ask QEMU to write a
# pidfile so we can terminate it immediately once the smoke summary appears
# (otherwise QEMU keeps running because PID 1 respawns the shell).
{
    sleep "$input_delay"
    printf "run /bin/smoke\nexit\n"
} | qemu-system-i386 -machine q35 -kernel "$kernel" -initrd "$initrd" -m "$mem" \
    -display none -monitor none -serial stdio \
    -pidfile "$pidfile" \
    -drive file="$scsi",format=raw,if=none,id=scsidisk0 \
    -device virtio-scsi-pci,id=scsi0,disable-modern=on \
    -device scsi-hd,drive=scsidisk0,bus=scsi0.0 \
    -drive file="$nv0",format=raw,if=none,id=nvme0 -device nvme,drive=nvme0,serial=NVME0001 \
    -drive file="$nv1",format=raw,if=none,id=nvme1 -device nvme,drive=nvme1,serial=NVME0002 \
    >"$tmp" 2>&1 &
jobpid="$!"

# Fallback: in POSIX sh, $! for a background pipeline is typically the PID of
# the last command (QEMU). Keep it as a best-effort kill target even if the
# pidfile is not available for some reason.
qpid="$jobpid"
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ -s "$pidfile" ]; then
        qpid="$(cat "$pidfile" 2>/dev/null || true)"
        break
    fi
    sleep 0.1
done

# Stop QEMU early once the test suite summary appears; otherwise enforce a hard timeout.
start_time="$(date +%s)"
deadline="$(( start_time + smoke_timeout ))"
timed_out=0
while :; do
    if ! kill -0 "$qpid" 2>/dev/null; then
        break
    fi

    if grep -q "All tests completed (OK)." "$tmp" 2>/dev/null; then
        kill "$qpid" 2>/dev/null || true
        break
    fi
    if grep -q "All tests completed (FAIL)." "$tmp" 2>/dev/null; then
        kill "$qpid" 2>/dev/null || true
        break
    fi

    now="$(date +%s)"
    if [ "$now" -ge "$deadline" ]; then
        timed_out=1
        kill "$qpid" 2>/dev/null || true
        sleep 1
        kill -KILL "$qpid" 2>/dev/null || true
        break
    fi
    sleep 0.2
done

wait "$jobpid" 2>/dev/null || true
if [ "$timed_out" -ne 0 ]; then
    elapsed="$(( $(date +%s) - start_time ))"
    {
        echo
        echo "SMOKE HARNESS TIMEOUT: mem=$mem timeout=${smoke_timeout}s elapsed=${elapsed}s"
        echo "SMOKE HARNESS TIMEOUT: QEMU was terminated before a smoke summary appeared."
    } >>"$tmp"
fi
cat "$tmp"
grep -q "All tests completed (OK)." "$tmp"
