#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
output="$(timeout 4 qemu-system-x86_64 \
  -drive format=raw,file=build/hypeai-os.img \
  -serial stdio \
  -display none \
  -no-reboot 2>&1 || true)"

printf '%s\n' "$output"
grep -q "HYPEAI OS BOOT OK - x86_64 long mode" <<<"$output"
echo "QEMU boot test passed"
