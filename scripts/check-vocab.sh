#!/bin/sh
set -eu

# Lightweight policy check: avoid introducing non-Linux terms into the Lite tree.
# Keep this list small and only add terms that we explicitly decided to forbid.

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

TOKENS="
platform-root
pci-root
devmodel_ready
devmodel_ksets_ready
device_model_inited
device_model_mark_inited
"

fail=0

echo "check-vocab: scanning for banned tokens..."

for t in $TOKENS; do
    # Ignore the vendored linux2.6 tree and build outputs.
    if grep -R -n -F "$t" "$ROOT_DIR" \
        --exclude=check-vocab.sh \
        --exclude-dir=linux2.6 \
        --exclude-dir=out \
        --exclude-dir=isodir \
        --exclude-dir=rootfs \
        --exclude-dir=.git >/dev/null 2>&1; then
        echo "ERROR: banned token found: $t"
        grep -R -n -F "$t" "$ROOT_DIR" \
            --exclude=check-vocab.sh \
            --exclude-dir=linux2.6 \
            --exclude-dir=out \
            --exclude-dir=isodir \
            --exclude-dir=rootfs \
            --exclude-dir=.git || true
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "check-vocab: FAILED"
    exit 1
fi

echo "check-vocab: OK"
