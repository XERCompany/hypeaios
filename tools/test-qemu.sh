#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
output="$(timeout 5 qemu-system-x86_64 \
  -machine pc \
  -vga std \
  -drive format=raw,file=build/hypeai-os.img \
  -serial stdio \
  -display none \
  -no-reboot 2>&1 || true)"

printf '%s\n' "$output"
grep -q "HYPEAI OS BOOT OK - HypeAICode++ x86_64 long mode" <<<"$output"
grep -q "core services running=yes" <<<"$output"
grep -q "font default=Arial registered=1" <<<"$output"
grep -q "edid blocks=2 extensions=1 checksum=ok" <<<"$output"
grep -q "monitor name=QEMU Monitor" <<<"$output"
grep -q "hdr static metadata not present" <<<"$output"
grep -q "CTA colorimetry block not present" <<<"$output"
grep -q "VBE framebuffer active; graphics primitives initialized" <<<"$output"
grep -q "HypeAICode++ runtime active" <<<"$output"
echo "QEMU boot and framebuffer test passed"
