# Research: HypeAI Hello auth + PCI audio stack

Gathered 2026-11-26 (subagent research for spec #13).

## Windows Hello-style authentication

Windows Hello organizes sign-in as per-user, per-device "Hello" containers; each gesture (PIN, face, fingerprint, security key) gets a unique **protector key** wrapping a shared **authentication key**. Password is fallback; security keys use FIDO2/WebAuthn.
Sources: https://learn.microsoft.com/en-us/windows/security/identity-protection/hello-for-business/how-it-works · https://learn.microsoft.com/en-us/windows/apps/develop/security/windows-hello

**Credential record** (per user, per method):
```
struct HelloCredential { u32 uid; AuthMethod method; u8 salt[16]; u8 verifier[32];
                         u8 wrapped_key[48]; u32 fail_count; u32 flags; }
```
`verifier` = PBKDF2/SHA-256(salt, secret) for Password/PinCode, or public key for Key/Face.

**PIN vs password**: PIN is device-bound entropy (TPM-sealed on Windows). Without TPM: derive `K = KDF(salt ‖ device_secret ‖ pin)`, keep device_secret kernel-side, software fail-count + backoff. Source: https://learn.microsoft.com/en-us/windows/security/identity-protection/hello-for-business/faq

**Match Face without camera** (user decision 2026-11-26): FaceProvider interface `capture() -> template`; on QEMU returns an explicit "no camera present" error — honest failure, not simulated success. Future UVC/webcam provider behind same interface.

**FIDO2/security key** (concept): registration `authenticatorMakeCredential` → ES256 keypair, store credential ID + pubkey; login `authenticatorGetAssertion` → device signs `authData‖clientDataHash` (ES256); host verifies. Kernel implements: challenge gen (RNG), ECDSA-P256 verify, credential store. Deferred: USB HID CTAP transport, CBOR. Simulate authenticator as virtual-key blob.
Sources: https://www.w3.org/TR/webauthn-2/ · https://www.itu.int/rec/T-REC-X.1278 · https://docs.canokeys.org/development/protocols/ctap2/

## PCI audio detection

Class `0x04` multimedia; subclass `0x01` audio (AC'97/ES1370), `0x03` HD Audio. Probe `find_class(0x04,0x03)` then `(0x04,0x01)`.

| QEMU `-device` | Vendor:Device | Class | Notes |
|---|---|---|---|
| `intel-hda` | 8086:2668 | 04/03 | ICH6 HDA |
| `ich9-intel-hda` | 8086:293e | 04/03 | ICH9 HDA (q35 default) |
| `AC97` | 8086:2415 | 04/01 | Intel 82801AA |
| `ES1370` | 1274:5000 | 04/01 | Ensoniq AudioPCI |
| `sb16` / `adlib` | — (ISA) | — | ports only, not PCI-probeable |

HDA needs codec device too: `-device intel-hda -device hda-duplex` (QEMU codec vid `1af4`).

## Intel HDA cheat sheet (BAR0 MMIO)

- `GCAP`(0x00) · `GCTL`(0x08) bit0 CRST reset · `STATESTS`(0x0E) codec-attached bits · `INTCTL`(0x20).
- CORB @0x40 / RIRB @0x50: 256-entry rings, 4B verbs / 8B responses; `CORBWP`(0x48), `CORBRP`(0x4A bit15 reset), `CORBCTL`(0x4C bit1 RUN), `CORBSIZE`(0x4E).
- Verb `[codec:4][node:8][cmd+param]`; vendor id = verb `0xF00` param `0x00` node 0 → `vid:16‖did:16`.
- Streams @0x80+n*0x20: `CTL`(bit0 RUN, stream# bits 23:20), `FMT`(w), `BDPL/BDPU`, `CBL`, `LVI`, `LPIB`. BDL = 16B entries `{addr64,len32,IOC}`.
- Playback: reset → codec wake → AFG widget walk (pin ctrl 0x707, EAPD, amp, DAC fmt 0x20000, conn 0x701) → BDL + RUN.
Sources: https://wiki.osdev.org/Intel_High_Definition_Audio · Intel HDA spec PDF

## AC'97 short

NAM BAR (BAR0) mixer regs: 0x00 reset, 0x02 master vol, 0x18 PCM-out vol. NABM BAR (BAR1) bus master @0x10 PCM-out: +0 BDL phys, +4 CIV, +5 LVI, +6 status, +0xB control bit0 run; global @0x2C. BDL ≤32×8B `{phys32, ctrl/len}`. Fixed 48 kHz.
Source: https://osdev.wiki/wiki/AC97

## Minimal audio service API (from ALSA model)

card→device→subdevice; `audio_probe() -> cards`; `card.pcm_open(fmt{rate,bits,ch}) -> stream`; `stream.write/start/stop/position`; ring buffer + IOC irq. Both HDA and AC'97 map to BDL models.

## ALAC

Reference decoder: https://github.com/macosforge/alac (`ALACDecoder.cpp`, Apache-2.0) and FFmpeg `libavcodec/alac.c`. Needs: 24/36B `ALACSpecificConfig` cookie, bitreader, Rice/Golomb residual decode, FIR predictor, stereo decorrelation. ~1.5k LOC for raw-packet decoder.

## WAV/PCM

RIFF `fmt ` chunk (1=PCM,3=float) + `data` chunk, interleaved LE. 8-bit unsigned, 16/24/32 signed LE. HDA/AC97 DMA wants interleaved S16LE stereo @48k.
