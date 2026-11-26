# Program: HypeAI Hello auth + audio subsystem

Spec: GitHub issue [#13](https://github.com/XERCompany/hypeaios/issues/13), closed 2026-11-26. All tickets #14–#22 merged into `devin/hypeai-font-ui`. Research report: `docs/research/hello-audio.md` (as of `2fb6fd8`+).

## What exists (as of commit `345add0`)

- **Crypto** (#14, `hcpp/`): SHA-256, HMAC-PBKDF2 (10k iter), RDRAND+TSC rng, ECDSA P-256 verify + sign (`ecdsa=ok`, `pbkdf2=ok`).
- **DB** (#15, `kernel/db/`): vendored SQLite 3.50.4 on native `hypevfs` memdb VFS; heap allocator over PMM; libc shims in `third_party/sqlite/freestanding/`; `sqlite=ok db selftest=ok`. Required kernel relocation to physical 0x100000 and KERNEL_SECTORS→4096 (image 4MB).
- **Hello core** (#17, `kernel/hello/`): `HelloMethod{Key,Password,PinCode,Face}`, enroll/auth, fail_count lockout at 5, credentials persisted in sqlite; device_secret in `hello_config` row. Password=PBKDF2(secret,salt); PinCode=PBKDF2(pin,salt‖device_secret).
- **Hello Key + Face** (#18): ECDSA challenge-response — challenge `sha256(uid‖counter‖device_secret)`, persistent `key_ctr` counter (replay-proof); pubkey in `wrapped_key` BLOB. Face honestly fails: enroll→false, auth→`NoCamera`.
- **Audio skeleton** (#16, `kernel/audio/`): the 20 module filenames from the spec exist; PCI class-code scan → `audio chips=N` (detects intel-hda, ichx/ac97, ES1370).
- **HDA driver** (#19): MMIO init, CORB/RIRB verb exchange, codec walk; `hda codec vid=1af40022` under QEMU `intel-hda`. QEMU quirks handled: RIRB IRQ ack per verb, NODE_COUNT start/count fields, CTL write while SRST.
- **PCM playback** (#20): AFG widget path DAC→pin, BDL 16-page contiguous PMM buffer, 440Hz sine @48k/16/stereo; `audio stream=run`, `hda lpib` advancing, `audio playback=ok`. QEMU 11.1 needs `hda-duplex,audiodev=snd0` binding (wired in Makefile/test-qemu.sh).
- **Decoders** (#22): real RIFF/WAV parse, ALAC cookie+element walk; `wav parse=ok`, `alac cookie=ok`.
- **Login UI** (#21, `kernel/ui/`): `flowlogin` module + `TextInput` widget (masked, caret); `ui login=shown` → auth → `ui login=ok` → desktop. Demo auto-enrolls `demo` user (PIN 2468).

## Open questions / known gaps (2026-11-26)

- User said "sqlite переписать под hypeai os core" mid-program; merged result is vendored SQLite on native VFS. If a pure from-scratch engine is wanted, ticket `hypedb` separately.
- Audio buffers and DB are memdb-only — nothing persists across boots (no disk write path yet).
- Toast lifetime still ticks per paint call; PIT exists but not wired to UI.
- `test-qemu.sh -daemonize` unsupported on Windows QEMU (pre-existing).

## Decisions (2026-11-26, user)

- Credential storage: sqlite3 inside the kernel (per spec answer), satisfied by #15 port.
- Face auth: fail with a proper "no camera" error — do not fake it.
- Audio: full stack with the 20 named files, real drivers where hardware exists in QEMU.
