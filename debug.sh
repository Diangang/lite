#!/bin/bash

# Clean build
echo "Cleaning build..."
make clean

# Compile
echo "Compiling..."
if ! make; then
    echo "Build failed!"
    exit 1
fi

# Run QEMU with timeout
echo "Running QEMU..."
# Run in background to capture PID
qemu-system-i386 -kernel myos.bin -initrd initrd.img -m 512M -serial stdio -display none &
QEMU_PID=$!

# Wait for a few seconds to let it boot and print logs
sleep 5

# Kill QEMU
echo "Killing QEMU (PID: $QEMU_PID)..."
kill -9 $QEMU_PID 2>/dev/null

echo "Done."
