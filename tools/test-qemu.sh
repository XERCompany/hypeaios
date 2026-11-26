#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p build
rm -f build/test-serial.log build/qemu-monitor.sock build/qemu.pid

qemu-system-x86_64 \
  -machine pc \
  -vga std \
  -drive format=raw,file=build/hypeai-os.img \
  -serial file:build/test-serial.log \
  -monitor unix:build/qemu-monitor.sock,server,nowait \
  -pidfile build/qemu.pid \
  -display none \
  -no-reboot \
  -daemonize

python3 - <<'PY'
import socket
import time

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
for _ in range(100):
    try:
        sock.connect('build/qemu-monitor.sock')
        break
    except OSError:
        time.sleep(0.02)
else:
    raise SystemExit('qemu monitor socket did not appear')

time.sleep(0.25)
sock.sendall(b'sendkey a 20\n')
time.sleep(0.45)
sock.sendall(b'quit\n')
sock.close()
PY

pid="$(cat build/qemu.pid 2>/dev/null || true)"
if [[ -n "$pid" ]]; then
  for _ in $(seq 1 100); do
    kill -0 "$pid" 2>/dev/null || break
    sleep 0.02
  done
  kill "$pid" 2>/dev/null || true
fi

output="$(cat build/test-serial.log)"
printf '%s\n' "$output"
grep -q "HYPEAI OS BOOT OK - HypeAICode++ x86_64 long mode" <<<"$output"
grep -q "core services running=yes" <<<"$output"
grep -q "core selftest=passed" <<<"$output"
grep -q "runtime boot=yes" <<<"$output"
grep -q "pci display=QEMU Standard VGA vendor=QEMU" <<<"$output"
grep -q "memory free=126MB e820=" <<<"$output"
grep -q "acpi=yes tables=" <<<"$output"
grep -q "interrupts=ready" <<<"$output"
grep -q "vmm=ready madt=yes cpus=1 ioapics=1 apic=enabled" <<<"$output"
grep -q "context switch=ready" <<<"$output"
grep -q "kernel threads=ready" <<<"$output"
grep -q "font default=Arial registered=1" <<<"$output"
grep -q "edid blocks=2 extensions=1 checksum=ok" <<<"$output"
grep -q "monitor name=QEMU Monitor" <<<"$output"
grep -q "hdr static metadata not present" <<<"$output"
grep -q "CTA colorimetry block not present" <<<"$output"
grep -q "VBE framebuffer active; graphics primitives initialized" <<<"$output"
grep -q "HypeAICode++ runtime active" <<<"$output"
grep -q "pit ticks after 100ms=10 interrupts active=yes" <<<"$output"
grep -q "keyboard events=2 decoder=ready input=a" <<<"$output"
echo "QEMU boot, memory, ACPI, interrupt, keyboard and framebuffer test passed"
