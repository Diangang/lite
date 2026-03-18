#!/bin/bash
set -euo pipefail

cd "$(dirname "$0")"

make clean >/dev/null
make >/dev/null

tmp_dir="$(mktemp -d)"
in_fifo="$tmp_dir/qemu.in"
out_file="$tmp_dir/qemu.out"

mkfifo "$in_fifo"

cleanup() {
  kill -9 "${qemu_pid:-0}" >/dev/null 2>&1 || true
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

qemu-system-i386 -kernel myos.bin -initrd initrd.img -m 512M -display none -serial stdio <"$in_fifo" >"$out_file" 2>&1 &
qemu_pid=$!

exec 3>"$in_fifo"
printf "ps\n" >&3
printf "run cat.elf\n" >&3

deadline=$((SECONDS + 10))
while [[ $SECONDS -lt $deadline ]]; do
  if grep -q "lite-os> " "$out_file"; then break; fi
  sleep 0.1
done

if ! grep -q "lite-os> " "$out_file"; then
  echo "boot timeout"
  tail -n 200 "$out_file" || true
  exit 1
fi

deadline=$((SECONDS + 10))
while [[ $SECONDS -lt $deadline ]]; do
  if grep -q "This is another file\\." "$out_file" && grep -q "exit: code=0" "$out_file"; then exit 0; fi
  sleep 0.1
done

echo "smoke failed"
tail -n 200 "$out_file" || true
exit 1
