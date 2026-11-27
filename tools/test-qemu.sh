#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
mkdir -p build
rm -f build/test-serial.log build/qemu-monitor.sock build/qemu.pid

qemu-system-x86_64 \
  -machine pc \
  -vga std \
  -drive format=raw,file=build/hypeai-os.img \
  -device intel-hda -device hda-duplex,audiodev=snd0 -audiodev none,id=snd0 \
  -device AC97,audiodev=snd0 \
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

def reconnect():
    global sock
    try:
        sock.close()
    except Exception:
        pass
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('build/qemu-monitor.sock')

def send(command):
    global sock
    for _ in range(5):
        try:
            sock.sendall(command)
            return True
        except (BrokenPipeError, ConnectionResetError, OSError):
            time.sleep(0.1)
            try:
                reconnect()
            except OSError:
                pass
    return False

time.sleep(0.25)
send(b'sendkey a 20\n')
time.sleep(0.45)
send(b'quit\n')
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
if ! grep -q "HYPEAI OS BOOT OK" <<<"$output"; then
  output="$(timeout 5 qemu-system-x86_64 \
    -machine pc -vga std \
    -drive format=raw,file=build/hypeai-os.img \
    -device intel-hda -device hda-duplex,audiodev=snd0 -audiodev none,id=snd0 \
    -device AC97,audiodev=snd0 \
    -serial stdio -display none -no-reboot 2>&1 || true)"
fi
printf '%s\n' "$output"
grep -q "HYPEAI OS BOOT OK - HypeAICode++ x86_64 long mode" <<<"$output"
grep -q "core services running=yes" <<<"$output"
grep -q "core selftest=passed" <<<"$output"
grep -q "runtime boot=yes" <<<"$output"
grep -q "pci display=QEMU Standard VGA vendor=QEMU" <<<"$output"
grep -q "memory free=1[12][0-9]MB e820=" <<<"$output"
grep -q "acpi=yes tables=" <<<"$output"
grep -q "interrupts=ready" <<<"$output"
grep -q "vmm=ready madt=yes cpus=1 ioapics=1 apic=enabled" <<<"$output"
grep -q "context switch=ready" <<<"$output"
grep -q "video devices=1 core=ready" <<<"$output"
grep -q "kernel threads=ready" <<<"$output"
grep -q "fonts=[0-9]" <<<"$output"
grep -q "default=hypeai-8x16" <<<"$output"
grep -q "hpf selftest=ok" <<<"$output"
grep -q "sqlite=ok" <<<"$output"
grep -q "db selftest=ok" <<<"$output"
grep -q "edid blocks=2 extensions=1 checksum=ok" <<<"$output"
grep -q "monitor name=QEMU Monitor" <<<"$output"
grep -q "hdr static metadata not present" <<<"$output"
grep -q "CTA colorimetry block not present" <<<"$output"
grep -q "VBE framebuffer active; graphics primitives initialized" <<<"$output"
grep -q "ui kit selftests=ok" <<<"$output"
grep -q "wm wins=2" <<<"$output"
grep -q "wm close=ok" <<<"$output"
grep -q "wm z=ok" <<<"$output"
grep -q "wm menu=ok" <<<"$output"
grep -q "wm selftest=ok" <<<"$output"
grep -q "session state=greeter" <<<"$output"
grep -q "session services=" <<<"$output"
grep -q "session reg=ok" <<<"$output"
grep -q "hello auth=pincode fail" <<<"$output"
grep -q "hello auth=pincode ok" <<<"$output"
grep -q "session auth=ok" <<<"$output"
grep -q "ss tiles=" <<<"$output"
grep -q "ss launch=ok" <<<"$output"
grep -q "ui shell=painted wins=1" <<<"$output"
! grep -q "ui demo" <<<"$output"
grep -q "HypeAICode++ runtime active" <<<"$output"
grep -q "pit ticks after 100ms=10 interrupts active=yes" <<<"$output"
grep -q "decoder=ready" <<<"$output"
grep -q "hda init=ok" <<<"$output"
grep -q "hda codec vid=" <<<"$output"
grep -q "audio stream=run" <<<"$output"
grep -q "audio playback=ok" <<<"$output"
grep -q "ac97 dev=ok" <<<"$output"
grep -q "ac97 civ>0" <<<"$output"
grep -q "ac97 vol=ok" <<<"$output"
grep -q "vcap frame=ok" <<<"$output"
grep -q "yuv conv=ok" <<<"$output"
grep -q "v4lv stream=ok" <<<"$output"
grep -q "video ctrl=ok" <<<"$output"
grep -q "video fmt=ok" <<<"$output"
grep -q "video core=ok" <<<"$output"
grep -q "svc:video running" <<<"$output"
echo "QEMU boot, memory, ACPI, interrupt, keyboard and framebuffer test passed"
