# Hardware Support Gap Analysis — HypeAI OS vs a "Serious" Hobby OS

Branch: `devin/p20-gapshw` — scope: bare-metal hardware coverage that a
serious hobby OS (Linux-on-metal class features minus userspace) would have
and HypeAI OS does **not** yet have. Items already implemented (PS/2
kbd+mouse w/ IRQ12 wheel, xHCI HID, AHCI, NVMe, ATA PIO, PCIe ECAM,
MSI/MSI-X, IOAPIC+LAPIC, HPET/TSC/PIT/RTC, SMP, virtio-net ARP/ICMP/UDP/DHCP,
VBE LFB, virtio-gpu present, 20+ PCI audio codecs incl. HDA/AC97/SB16/OPL2,
MPU-401, FireWire OHCI+AMDTP, KVM/VMX skeleton, 16550 serial, FAT32) are
excluded.

Method: cross-checked against osdev.wiki, the Linux driver tree
(`drivers/{usb,net,mmc,ata,char/tpm,watchdog,hid,media,bluetooth}`), vendor
datasheets (Intel PRMs, Realtek programming guides), and the public spec
documents named below. Effort ratings are calibrated for *our* codebase —
i.e. they assume the existing infrastructure hooks named in each section:

    S  < ~500 lines, days            M  ~1–3k lines, 1–2 weeks
    L  ~3–10k lines, weeks           XL >10k lines / open-ended

## Hook points in our tree (named once, reused below)

| Our component | Path | Role for new drivers |
|---|---|---|
| Block dispatch | `kernel/block/blockdev.{h,cc}` | `block_read/write/flush(lba, buf, n)` — the single "sector I/O" API every storage driver feeds; today picks NVMe > AHCI > ATA |
| Net stack | `kernel/net/net.h`, `virtio_net.cc` | `net_udp_*`, `net_arp_resolve`, poll-driven RX; a new NIC driver just provides `send(frame)`/`recv(frame)`/MAC |
| USB stack | `kernel/usb/xhci.{h,cc}` | Only layer that talks to xHCI: control/bulk/interrupt-IN transfers, slot/endpoint context mgmt; hub + iso missing |
| Input | `kernel/input/` (`msaux.h`, `scancode.h`, `hypeptr.*`, `keyring.*`) | USB HID already feeds `scancode_decode()` + `MsauxPacket` queue; new HID classes should reuse this |
| Video core | `kernel/video/videov4lv.h`, `videostream.h`, `videocapture.h` | V4L-ish device registry + ring-buffer streaming (`stream_queue/dequeue/push/pop`) — the natural landing pad for a webcam |
| Audio | `kernel/audio/audioapi.h`, `hda.{h,cc}`, `audiocore.*` | `AudioFormat`/`AudioStreamId` mixer-facing API; USB audio becomes another stream source |
| ACPI | `kernel/acpi/acpi.{h,cc}` (+`madt`, `mcfg`) | Table walker (`find_table`) — FADT already located; needed for S-states, TPM2 table, thermal |
| Power | `kernel/power.{h,cc}` | `power_shutdown()/power_restart()` — port-0x604/0xB004 + 8042 reset already done |
| VFS/FS | `kernel/vfs/` (`fat32.*`, `hypefs.*`, `hypeblob.*`, `fdtable.*`) | FAT32 exists; new FS modules mount on the same `hypefile`/`vfile` layer |
| Display/GPU | `kernel/xdp/xdp.h`, `backend_sw.cc`, `backend_virtgpu.cc`, `kernel/display/` (EDID, DP helpers) | `xdp_backend_register()` + `XDP_CAP_{2D,PRESENT,COMPUTE,VULKAN,OPENCL}` ladder is explicitly designed for future native GPU backends |
| Virtio | `kernel/virtio/vring.h` | Shared legacy split-vring transport — virtio-rng/-watchdog/-scsi are mostly free on top |
| Runtime stages | `kernel/runtime/runtime.cc` | `RuntimeStage{name,prio,enter}` table — every new subsystem registers one stage (e.g. `"usb.xhci" @57`) |
| CPU misc | `kernel/arch/x86/` (`cpuid.*`, `tsc.*`, `io.h`) | CPUID/MSR helpers for RDRAND, thermal MSRs, MWAIT C-states, HWP |
| Security | `kernel/guard/` (guardapi, landlock-style rules) | Natural consumer of TPM attestation / RNG entropy |

---

## 1. USB storage and USB stack breadth

### 1.1 USB Mass Storage — Bulk-Only Transport (BOT) + UAS  — Effort M / Value ★★★★★
*What:* the class behind every USB stick/hard-drive/enclosure. BOT ("BBB" in
specs) tunnels a SCSI command set inside a 31-byte **CBW** (sig `0x43425355`)
→ bulk-IN/OUT data → 13-byte **CSW** (sig `0x53425355`) handshake.
*Specs:* USB-IF *Mass Storage Class Bulk-Only Transport r1.0* (`usbmassbulk_10.pdf`),
*MSC Overview + UFI command spec*, T10 SPC/SBC for the SCSI verbs
(INQUIRY 0x12, TEST UNIT READY 0x00, READ CAPACITY(10) 0x25, READ(10) 0x28,
WRITE(10) 0x2A, REQUEST SENSE 0x03). osdev.wiki "USB Mass Storage Class
Devices" documents the CBW/CSW layout directly.
*UAS (optional upgrade):* T10/2095-D "USB Attached SCSI" + USB-IF UASP class
spec; needs xHCI *streams* for command queueing — defer.
*Hook:* `kernel/usb/xhci` bulk transfers → new `kernel/usb/usbms.cc` →
register a backend into `kernel/block/blockdev` priority chain (alongside
NVMe/AHCI). The blockdev API is already exactly `lba+sectors→buffer` so the
mapping is trivial. Requires endpoint-addressing beyond the current
boot-protocol-HID subset. STALL/CSW retry handling is the fiddly part.
Linux ref: `drivers/usb/storage/{usb,transport,scsiglue}.c`.

### 1.2 UHCI / OHCI / EHCI companion host controllers — Effort L / Value ★★★★
*What:* pre-xHCI root hubs. On many real PCs (esp. ≤2010, and machines where
BIOS leaves EHCI enabled) full/low-speed devices (keyboards, mice, UPSs,
bootloaders) hang off EHCI or OHCI, not xHCI. Also needed because xHCI
"companion" handoff is what the EHCI "owned semaphore" exists for — a device
plugged into a shared port may be EHCI-visible until we explicitly route it.
*Specs:* Intel *UHCI 1.1 spec* (iobase frame list + TD/QH),
*OpenHCI 1.0a* (MMIO ED/TD lists, HCCA), Intel *EHCI 1.0* (async QH +
periodic iTD/siTD frame list). osdev.wiki has pages for each; Linux
`drivers/usb/host/{uhci,ohci,ehci}-*.c`.
*Hook:* same `kernel/usb/` dir; refactor today's "xHCI is the USB layer" into
"host controllers feed a common USB device/core layer" (a `usbcore`
abstraction: enumerate → class driver). Without this, every class driver we
add is xHCI-only. Do *EHCI first* (USB 2.0 full-speed on real silicon,
Companion HC for FS/LS), then OHCI (older AMD/Compaq boards), UHCI last.

### 1.3 USB hub class — Effort M / Value ★★★★★
*What:* the class driver for hubs, including the xHCI-internal root hub.
Without it we only see root-port devices — real users plug hubs, dock
stations, and keyboards-through-monitors. Requires port status-change
interrupt endpoints, power-on sequencing per port (`PORT_POWER`,
`C_PORT_CONNECTION` debounce ~100 ms), device-address assignment for
children, and TT (transaction translator) handling for FS/LS devices behind
USB2 hubs.
*Specs:* USB 2.0 spec ch.11 (hub class), xHCI spec §4.3; osdev.wiki "USB hub".
*Hook:* `kernel/usb/xhci.cc` — today "NOT covered (deliberate): USB hubs,
non-root-port devices". Unlocks *everything else on this list on real desks*.

### 1.4 USB CDC-ACM serial + CDC-ECM/NCM Ethernet — Effort M / Value ★★★★
*What:* CDC-ACM = virtual COM port (Arduinos, 3D printers, debug consoles,
modems). CDC-ECM/NCM = USB Ethernet (tethering phones, USB dongles).
*Specs:* USB-IF *CDC 1.2 + PSTN subclass* (ACM), *CDC ECM 1.2 / NCM 1.0*.
osdev.wiki "CDC". Register-level: control pipe requests `SET_LINE_CODING`
0x20 / `GET_LINE_CODING` 0x21, `SET_CONTROL_LINE_STATE` 0x22; data on bulk
endpoints.
*Hook:* CDC-ACM → expose behind `serial::` (extend `kernel/serial.cc` into a
port abstraction). CDC-ECM → `kernel/net` NIC provider alongside
`virtio_net` — the net stack is already driver-agnostic.

### 1.5 USB UVC webcam — isochronous video — Effort L / Value ★★★
*What:* USB Video Class — every laptop webcam. Needs xHCI **isochronous
transfer descriptors** (we do none today), alternate-interface bandwidth
negotiation, and frame reassembly (UVC header + MJPEG/YUYV payloads).
*Specs:* USB-IF *UVC 1.5* spec set; Linux `drivers/media/usb/uvc/`.
*Hook:* `kernel/video/videov4lv.h` — register a `VideoDeviceInfo` with
`CAP_CAPTURE|STREAMING`, deliver frames through `VideoStream` ring
(`stream_push`), decode MJPEG via `kernel/video/videomcodec`/`videodecoder`
path. Big blocker: isochronous TRBs in `xhci.cc`.

### 1.6 USB HID beyond boot protocol — joystick/gamepad/tablet — Effort M / Value ★★★
*What:* our HID path is "boot-protocol keyboard/mouse only". Real HID
requires parsing report descriptors (usage pages, report IDs, item sizes) —
the messy part — then mapping gamepad axes/buttons.
*Specs:* USB-IF *HID 1.11 + HID Usage Tables*. Linux `drivers/hid/`.
*Hook:* `kernel/usb/xhci.cc` interrupt-IN path already exists; add a report
descriptor parser + a `kernel/input/gamepad.*` consumer exposing axes to
`hypeptr`/`ui`. Wacom tablets = same HID descriptor path + absolute
coordinate routing into `hypeptr`.

---

## 2. Storage beyond NVMe/AHCI/ATA-PIO

### 2.1 SDHCI — SD card & eMMC — Effort L / Value ★★★★★
*What:* SD Host Controller Interface — PCI class 0x08/0x05. Covers both
removable SD and soldered eMMC on cheap laptops/netbooks where there's no
SATA at all.
*Specs:* SD Association *SD Host Controller Standard Spec (simplified) v4.20*
(the simplified PDF has the full register map + SDMA/ADMA2 descriptor
formats); *SD Physical Layer Simplified Spec* for the SD command set
(CMD0/8/9/17/24, ACMD41/51, RCA); JEDEC *JESD84-B51* for eMMC 5.1 commands.
Linux: `drivers/mmc/host/sdhci{,-pci}.c`, `drivers/mmc/core/`.
*Register primer:* MMIO — `SDMA addr 0x00`, `BLOCK_SIZE/COUNT 0x04`,
`ARGUMENT 0x08`, `TRANSFER_MODE/COMMAND 0x0C-0x0F`, `RESPONSE 0x10`,
`PRESENT_STATE 0x24`, `POWER/CLOCK/TIMEOUT 0x28-0x2F`, `INT_STATUS 0x30`,
`CAPABILITIES 0x40`, `HOST_VERSION 0xFE`.
*Hook:* `kernel/block/blockdev` — new `sdhci_probe()` feeding the same
`block_read/write` dispatch. Watch: eMMC vs SD command differences at init,
and HS/DDR timing (start with default-speed 25 MHz, it "just works").

### 2.2 ATAPI CD-ROM + ISO9660 read — Effort M / Value ★★★
*What:* optical discs on the IDE/ATAPI layer we already half-have; ATAPI wraps
SCSI *MMC* commands (READ(10/12), READ TOC, START STOP UNIT) in ATA PACKET
(0xA0). Needed to read install media / old discs.
*Specs:* ATA/ATAPI-8 (T13), SCSI MMC-6 (T10), ISO9660 + Rock Ridge/Joliet.
*Hook:* extend `kernel/ata.cc` (already does PIO 0x1F0; add PACKET + DRQ
handling) → `kernel/block/blockdev` with 2048-byte sector size (blockdev is
512-leaning — needs a block-size field) → `kernel/vfs/iso9660.cc`.
SATA ATAPI via AHCI: `ahci.cc` already builds FIS commands; ATAPI adds the
PACKET FIS type — small delta.

### 2.3 Floppy — intentionally excluded
Legacy FDC 0x3F0 + DMA channel 2; zero modern-hardware coverage, QEMU-only
value. Skip per project decision. (The ISA DMA controller glue alone costs
more than it returns.)

### 2.4 SATA depth: hotplug, NCQ, multi-port, ATAPI, PM — Effort M / Value ★★★
*What:* `ahci.cc` currently does single-active-port, non-queued FIS commands.
Gaps: (a) **NCQ** — READ/WRITE FPDMA QUEUED (0x60/0x61) with 32 command slots
instead of slot-0-only — real throughput win on spinning rust and SSDs;
(b) **hotplug** — PxIE interrupts + PxSERR handling to re-probe on
insert/remove; (c) **multi-disk** — blockdev today picks *one* AHCI port;
(d) **port multiplier** — defer.
*Specs:* AHCI 1.3.1 §9 (FIS), §10 (NCQ); SATA 3.x.
*Hook:* `kernel/block/ahci.cc` internals + `blockdev` grows a per-device
table instead of a singleton backend.

### 2.5 virtio-blk / virtio-scsi (bonus, nearly free) — Effort S / Value ★★
We have `virtio/vring.h` and the legacy transport. virtio-blk is ~200 lines
on top (feature bits, one request virtqueue, status byte). virtio-scsi a bit
more. Great for testing storage code under QEMU `-drive if=virtio` and on
cloud instances that never had IDE.

---

## 3. Filesystem media (read-only tier mostly)

### 3.1 exFAT — Effort M / Value ★★★★
Default FS on SDXC/USB sticks >32 GiB — the format files actually arrive on.
Microsoft published the spec (2019): simple FAT chain + bitmap + directory
entry sets (file entry + stream-ext + name entries, UTF-16 names). Read is
easy; write = allocate from bitmap (M-). Hook: `kernel/vfs/` alongside
`fat32.cc`.

### 3.2 ext2 (read) — Effort M / Value ★★★★
The gateway drug to Linux interop: superblock @1024, group descriptors,
inode table, direct/indirect block pointers. Read-only ext2 ≈ 1.5k lines.
Hook: `kernel/vfs/ext2.cc`. Unlocks reading Linux partitions — and ext3/4
*read* largely works by ignoring the journal.

### 3.3 NTFS read — Effort L / Value ★★★
MFT parsing, attribute runs, resident/non-resident data, `$Bitmap`.
Reference: Linux `fs/ntfs3` (read path), ntfs-3g docs, "NTFS Documentation"
by Richard Russon/ntfsdoc. Read-only is tractable; write is XL — don't.

### 3.4 ISO9660 + Joliet/Rock Ridge, UDF — Effort M (ISO) / L (UDF) / Value ★★
ISO9660: PVD at sector 16, directory records, path table; Joliet = SVD with
UTF-16; Rock Ridge = POSIX attrs (skip initially). UDF (DVD/BD) is a
beast — ECMA-167 + OSTA UDF, VAT/anchor games; defer unless optical media
becomes a real use case. Hook: `kernel/vfs/` over ATAPI/UDF-aware blockdev.

---

## 4. Networking — real NICs

### 4.1 Intel e1000 (8254x PCI) — Effort M / Value ★★★★★
*The* best-documented NIC in existence and QEMU's `-nic model=e1000`.
*Specs:* Intel *8254x Family GbE Controllers SDM*, *PCIe GbE Controllers
Open Source SDM* (covers 82571/2/3 → e1000e). osdev.wiki "Intel 8254x".
TX/RX descriptor rings (16-byte descriptors), RCTL/TCTL, IMS mask,
EEPROM via EERD. ~1k lines for a solid driver.
*Hook:* `kernel/net` — add `e1000_probe()` alongside `virtio_net_probe()`;
net.c already routes via a NIC-agnostic frame interface.

### 4.2 Intel e1000e (8257x/ICH LAN: I217, I219, 82577LM…) — Effort M→L / Value ★★★★★
What real laptops/desktops actually ship. Mostly e1000-compatible plus NVM
(NVM/iNVM vs EEPROM quirks), PHY negotiation via MDIC/Kumeran regs, and
SPT/LPT LP differences. osdev.wiki "Intel Ethernet i217" documents a working
hobby driver on Thinkpads (device IDs 0x153A, 0x10EA…). Linux `e1000e/`.
Start: QEMU `e1000-82545em`, then I217-class.

### 4.3 Realtek RTL8139 — Effort S / Value ★★★
The "hello world" NIC: IO-port regs (MAC0-5 @0x00, RBSTART @0x30, CMD @0x37,
CAPR/IMR/ISR), 8 KiB linear RX ring, 4 TX descriptors. ~300 lines. Only
10/100, still emulated by QEMU/VirtualBox and found in junk-bin hardware.
osdev.wiki "RTL8139" has the full register table. Hook: `kernel/net`.

### 4.4 Realtek RTL8169/8168/8111 (r8169) — Effort M / Value ★★★★
What a huge fraction of real desktops actually have (Realtek owns the cheap
GbE market). Descriptor rings similar to e1000 but with the "RTL8169*S*"
chip-variant matrix and PHY quirks. *RTL8169(S) Programming Guide*
+ osdev.wiki page; Linux `drivers/net/ethernet/realtek/r8169*.c`
(device IDs 10ec:8168/8169…). Hook: `kernel/net`.

### 4.5 virtio-1.0 modern transports + virtio-net thereon — Effort M / Value ★★★
Today's `virtio/vring.h` is *legacy* (transitional) only: port-IO BAR, PFN
queue addressing. Virtio 1.0 spec defines the modern capability-based PCI
layout (common cfg / notify / ISR / device cfg caps) and packed/virtqueue
options. Needed because some hypervisors expose only non-transitional
devices (device IDs 0x1041+). Hook: `kernel/virtio/` — add `vring_modern.*`;
consumers (net, future blk/rng/watchdog) get it free.

### 4.6 WiFi — honest assessment — Effort XL / Value ★★★ (theoretical)
*Intel (iwlwifi):* every Intel WiFi NIC since 2008. Requires loading a large
signed-ish **firmware blob** (`.ucode` from linux-firmware) through a paging
mechanism, then driving TX/RX/command TFD rings. The firmware API has
churned repeatedly (iwlmvm vs the new iwlmld op-modes); Linux iwlwifi alone
is ~150k lines. No public register-level datasheet — you're porting Linux
driver behaviour, not reading a spec.
*Atheros (ath9k, AR92xx/AR93xx):* the *only* realistic hobby path — firmware
runs on an on-chip Xtensa core and is burned into ROM, so the driver is a
straightforward register/ring driver (~ath9k hardware layer is self-contained).
Still needs a real 802.11 stack on top.
*The real cost is the 802.11 stack itself*: scanning, association,
WPA2-PSK (CCMP/AES + 4-way handshake) — none of which exists. Open/WEP for a
demo maybe; WPA2 is an L project alone.
*Verdict:* ath9k + WPA2-Personal = XL total. Defer; buy a $10 USB-Ethernet
dongle and do CDC-ECM instead.

### 4.7 Bluetooth USB HCI — Effort L / Value ★★
BT dongles/on-board BT speak *HCI over USB* (bulk/iso endpoints, HCI
commands/events). Spec: Bluetooth Core spec vol.4 (HCI), USB transport spec.
Host side = HCI → L2CAP → {RFCOMM/BNEP}. Useful for input devices (BT
keyboards/mice = HIDP) and audio (A2DP — needs SBC codec). The USB plumbing
part is S-M; the protocol stack above it is the L. Defer until hub + USB
core refactor exist.

---

## 5. Platform, power, health

### 5.1 TPM 1.2/2.0 — TIS FIFO + CRB — Effort M / Value ★★★★
*What:* attestation + sealed storage + RNG. Two MMIO interfaces at
**0xFED40000**: **TIS FIFO** (localities 0–4, `ACCESS`/`STS`/`DATA_FIFO`
regs — TCG *PC Client TIS 1.3*) and **CRB** (`CTRL_REQ/START`, command/
response buffer addrs — TCG *PTP 2.0 spec*). Discovered via ACPI `PNP0C31`/
`MSFT0101` device + `TPM2`/TCPA tables; QEMU exposes swtpm with both.
Commands are the TPM2 marshal format — `TPM2_GetRandom`, `PCR_Read/Extend`,
`NV_Read/Write` for sealed disk keys.
*Hook:* new `kernel/security/` (or `kernel/guard/tpm*`) — feeds `guard`'s
attestation story; `TPM2_GetRandom` also feeds the RNG pool (§5.5). M effort
= interface + marshalling for a handful of commands.

### 5.2 ACPI S3 suspend/resume — Effort XL / Value ★★
Needs: AML interpreter for `_S3/_S5/_WAK/_PTS/_TTS` methods (the killer
dependency — full AML is an OS project by itself; a minimal AML evaluator
for fixed signatures ≈ L), FADT `SLEEP_CONTROL/STATUS` or `PM1a/b_CNT`
SLP_TYPx writes, waking-vector re-entry preserving kernel state, device
suspend ordering, and restoring APIC/PCI/HPET state on resume.
*Realistic split:* (a) **proper power-off** via FADT PM1a_CNT `SLP_EN|SLP_TYP(_S5)`
— S, do now (we only have QEMU ports); (b) S4/hibernate — skip; (c) S3 —
XL, gated on AML.

### 5.3 ACPI shutdown via PM1 (finish the job) — Effort S / Value ★★★★
`power.cc` does QEMU 0x604 / PIIX4 0xB004 then parks. Real PCs need FADT
`PM1a_CNT_BLK`/`PM1b_CNT_BLK` write of `SLP_TYPx(_S5)|SLP_EN`. Parse `_S5`
from DSDT (needs mini-AML or a signature-scan hack). Half a day.

### 5.4 CPU C-states (idle) — Effort M / Value ★★★
Deeper idle than HLT: MWAIT hints from ACPI `_CST` (or Intel default table),
mwait in the idle loop, handling monotonicity between cores. Needs the
scheduler idle path (`kernel/sched/`). Moderate payoff on battery/latency;
largely invisible otherwise.

### 5.5 RNG — RDRAND/RDSEED + virtio-rng — Effort S / Value ★★★★★ (best ratio on the page)
CPUID.01H:ECX bits 30/31 → `rdrand`/`rdseed` instructions (~50 lines in
`kernel/arch/x86/`); virtio-rng is a one-virtqueue entropy device (device
0x1005/0x1044) on the existing `vring.h`. Feed an entropy pool + a
`kentropy()`/`rand_u64()` API consumed by crypto, TCP ISNs (when TCP lands),
guard tokens. Also see TPM `GetRandom`. Do it this week.

### 5.6 Watchdog — iTCO + virtio-watchdog — Effort S–M / Value ★★★
*iTCO:* Intel TCO watchdog lives in the LPC bridge PCI device (vendor 8086,
various dev IDs): TCO1_CNT in I/O space, NMIs or reboot on expiry — enables
"kernel hung → auto-reset" for headless operation.
*virtio-watchdog:* one virtqueue, trivial on `vring.h`.
Hook: a `kernel/watchdog/` stage that pet-dogs from the timer tick unless
`sched` reports all-threads-stalled.

### 5.7 hwmon — CPU temp, fans, thermal zones — Effort M / Value ★★★
*CPU temp:* Intel = MSR `IA32_THERM_STATUS` (0x19C) digital-readout vs
Tjunction (CPUID); AMD K8+ = PCI config device 18.3 regs (k8temp) / 17h+
SMN (k10temp). No ACPI needed → easy.
*ACPI thermal zones:* `_TMP`/`_CRT`/`_PSV` — AML again; skip until §5.2's
mini-interpreter exists.
*Fan control:* board-specific EC/SuperIO (ITE/Nuvoton on IO ports) — do one
cheap chip (NCT6775-class datasheet is public) rather than generic.
Hook: `kernel/chips/` (already a grab-bag of vendor chips) + a small
`hwmon` service dumping to serial/`ui`.

### 5.8 cpufreq — P-states / Intel HWP / AMD — Effort M / Value ★★
Legacy `_PSS` = AML (defer). Hardware P-states: Intel SpeedStep =
`IA32_PERF_CTL` (0x199) ratio writes; newer = HWP `IA32_HWP_REQUEST`
(0x774). AMD = `PSTATE_CTL` MSRs. Meaningful for battery only after C-states;
nice demo for "OS controls silicon".

### 5.9 APM — skip
16-bit BIOS-era power interface, dead hardware for 20 years. Note only.

### 5.10 Smart battery (ACPI) — Effort M / Value ★★★
`PNP0C0A` device, `_BIF`/`_BST` methods → percent/wh/ETA; data flows over
the EC's SMBus (Smart Battery spec, SBS). Same AML blocker — parked behind
§5.2, but the SBS protocol itself is documented and small once you can read
the EC.

---

## 6. Audio & peripherals

### 6.1 USB Audio Class (UAC1/UAC2) — Effort L / Value ★★★★★
*Highest-value audio item*: USB headsets, DACs, conference pods — this is
where real audio lives on modern machines. Needs isochronous xHCI transfers
(same blocker as UVC), alternate-setting selection, feedback endpoints
(async sinks), class-specific requests (SET_CUR on Feature Unit for volume).
UAC1 (every cheap dongle) ≪ UAC2.
*Specs:* USB-IF *Audio Device Class 1.0/2.0*, Terminal Types doc; Linux
`sound/usb/`.
*Hook:* `kernel/usb/` class driver → `kernel/audio/audiocore` as a new
stream source behind `AudioFormat`/`audio_stream_*` — the mixer API is
already exactly the right shape.

### 6.2 HDA HDMI/DP audio output — Effort M / Value ★★★
`hda.cc` drives local codec paths; HDMI audio = a *display* codec on the
same HDA bus + ELD (EDID-Like Data) handshake + pin widget routing. Our
`kernel/display/edidcta.cc` already parses EDID CTA — half the plumbing is
in. Requires the Intel/AMD GPU to actually expose the HDA function (present
on real hw, absent in QEMU by default). Specs: Intel HDA spec + HDMI spec's
ELD section; Linux `sound/hda/` `patch_hdmi.c`.

### 6.3 USB printer class — Effort S / Value ★★
Class 0x07: bulk-out IEEE-1284 frames, `GET_DEVICE_ID` control request
returns the IEEE-1284 ID string. Literally a weekend once bulk transfers
exist. PostScript/PCL output is a *formatter* problem (existing codecapps
could emit PS). Specs: USB-IF *Printing Device Class 1.1*.

### 6.4 Parallel port (IEEE 1284 SPP/ECP) — Effort S / Value ★
0x378 SPP mode, status/strobe pins. Retro-only value. Cheap.

### 6.5 Serial deeper — CDC-ACM (above) + PCI 16550 cards + FIFO — Effort S / Value ★★
`serial.cc` already does 16550 init (FCR 0xC7 set). Gaps: PCI multiport
serial cards (class 0x07/0x00/0x02 — BAR-based, trivial), FIFO
trigger-depth tuning, modem status lines (MSR/CTS flow control), interrupt
driven TX for big logs.

---

## 7. GPU landscape — honest assessment

### 7.1 Modesetting already solved-ish
VBE LFB + EDID (`display/edidcta.cc`) + DP helpers give scanout on nearly
anything that runs UEFI/legacy video BIOS. Native modesetting (Intel
`i915` display pipes, AMD DCN) = per-gen register soup — XL each, and XDP
already flips surfaces fine without it. *Do not chase native modesetting.*

### 7.2 Intel GPU 2D/blitter + GPGPU — Effort L→XL / Value ★★★★ (compute) / ★★ (2D)
Intel is the *only* vendor with fully public GPU docs: the **Intel OSRC
Graphics PRMs** (e.g. SKL volumes: Vol 5 Memory Views, Vol 6 Command Stream
Programming — RINGBUF/execlists, RCS/BCS command streamers, MI_* commands;
Vol 7 3D-Media-GPGPU for EU/SIMD kernel dispatch). Feasible path: GuC-free
legacy submission — execlist → RING_TAIL with `MI_BATCH_BUFFER_START`,
blitter (`XY_SRC_COPY_BLT`) for 2D accel, `MEDIA_OBJECT`/`GPGPU_WALKER` for
compute. Pitfalls: GGTT aperture (BAR2) + fence management, per-gen
workaround tables (Vol 16 "Workarounds" exists for a reason), stolen memory
(GGC reg).
*Hook:* `kernel/xdp` — `XDP_CAP_2D` backend on BCS; `XDP_CAP_COMPUTE` on
RCS. Start with the blitter (self-contained, huge blit speedup), park GPGPU
until shader infrastructure exists.

### 7.3 AMD GPU — Effort XL / Value ★
Register specs partially public (Radeon "SI/CI" era docs are stale;
amdgpu kernel driver ~2M lines incl. DAL/DCN). Plus PSP firmware loading.
Out of reach for a hobby OS; note and move on.

### 7.4 NVIDIA (nouveau-class) — Effort XL / Value ★
Reverse-engineered; modern cards need signed firmware (GSP) loaded via
proprietary blobs. Skip.

### 7.5 GPU compute / OpenCL for XDP ladder — feasibility note
`xdp.h` already reserves `XDP_CAP_OPENCL`/`XDP_CAP_COMPUTE`. Realistic
minimum viable path: **Intel Gen9 GPGPU** — PRM Vol 7 documents the EU ISA
and `GPGPU_WALKER`/`MEDIA_STATE_FLUSH` submission. You'd still need a
compiler for the EU instruction set (OpenCL front-end via clang+intel-gen
backend is a toolchain project unto itself; a hand-rolled mini-DSL emitting
EU binaries is the pragmatic route). Est: XL, but it is the *only* path on
real hardware with zero reverse engineering. Alternative: extend the
virtio-gpu backend with Venom/Virgl-style command offload in QEMU — M effort
for a demo, zero real-hardware value.

### 7.6 virtio-gpu 2D depth + virtio-1.0 GPU — Effort M / Value ★★★
Current backend is present-only. Adding `RESOURCE_CREATE`/`ATTACH_BACKING`
for guest-side buffers, `TRANSFER_TO_HOST_2D` batching, and a modern
(1.0) transport widens emulator coverage.

---

## 8. Input devices

### 8.1 PS/2 touchpads — Synaptics absolute mode — Effort S–M / Value ★★★★
*What:* laptops have no mouse — they have a PS/2 **touchpad**. The Synaptics
protocol is semi-public (widely mirrored protocol guide): magic knock
(`set sample rate 200→100→200` + read ID → `0x47`), mode byte `absolute
mode|high rate`, then 6-byte packets with absolute X/Y/Z/W — 4.8 in/mm
coordinates.
*Hook:* `kernel/input/msaux.cc` already parses byte streams; add a
`synaptics.cc` sibling emitting absolute `MsauxPacket`→`hypeptr`. Also
generic PS/2 "touchpad mode" for ALPS/Elan (different IDs).

### 8.2 USB gamepads/joysticks — see §1.6 (HID report descriptors).

### 8.3 Wacom tablets — see §1.6; absolute positioning into `hypeptr`.

---

## 9. Summary table

| # | Subsystem | Spec / reference source | Effort | Value | Hook point in our tree |
|---|---|---|---|---|---|
| 1 | USB mass storage (BOT) | USB-IF usbmassbulk_10.pdf; T10 SPC/SBC; osdev.wiki "USB MSC" | M | ★★★★★ | `kernel/usb/` class drv → `kernel/block/blockdev` |
| 2 | USB hub class | USB 2.0 ch.11; xHCI §4.3 | M | ★★★★★ | `kernel/usb/xhci.cc` (explicit TODO in header) |
| 3 | SDHCI (SD/eMMC) | SD Assoc Host Ctrl Simplified v4.20; JEDEC eMMC 5.1 | L | ★★★★★ | `kernel/block/blockdev` (`sdhci_probe`) |
| 4 | Intel e1000 | Intel 8254x SDM; osdev.wiki | M | ★★★★★ | `kernel/net` NIC provider |
| 5 | Intel e1000e (I217/I219…) | PCIe GbE OSS DM; osdev.wiki "i217"; Linux e1000e | M–L | ★★★★★ | `kernel/net` |
| 6 | RTL8169/8111 | Realtek prog. guide; Linux r8169 | M | ★★★★ | `kernel/net` |
| 7 | RTL8139 | osdev.wiki reg table | S | ★★★ | `kernel/net` |
| 8 | UHCI/OHCI/EHCI | Intel UHCI/EHCI specs; OpenHCI 1.0a; Linux host/*.c | L | ★★★★ | `kernel/usb/` (needs usbcore split) |
| 9 | USB CDC-ACM serial | USB-IF CDC 1.2 + PSTN | S–M | ★★★★ | `kernel/serial.cc` port layer |
| 10 | USB CDC-ECM/NCM net | USB-IF ECM/NCM | M | ★★★★ | `kernel/net` NIC provider |
| 11 | USB UVC webcam | UVC 1.5; Linux uvc/ | L | ★★★ | `kernel/video/videov4lv.h` + `videostream` (needs xHCI iso) |
| 12 | USB Audio UAC1/2 | USB-IF ADC specs; Linux sound/usb | L | ★★★★★ | `kernel/usb` → `kernel/audio/audiocore` |
| 13 | HDA HDMI audio | Intel HDA spec + ELD; Linux patch_hdmi | M | ★★★ | `kernel/audio/hda.cc` + `display/edidcta` |
| 14 | USB printer class | USB-IF Printing 1.1 | S | ★★ | `kernel/usb` bulk-out |
| 15 | ATAPI CD-ROM + ISO9660 | ATA/ATAPI-8; MMC; ISO9660 | M | ★★★ | `kernel/ata.cc`/`ahci.cc` → `blockdev` → `kernel/vfs` |
| 16 | SATA NCQ/hotplug/multi-disk | AHCI 1.3.1; SATA 3 | M | ★★★ | `kernel/block/ahci.cc` + `blockdev` per-device table |
| 17 | virtio-blk/scsi + virtio-1.0 | Virtio 1.2 spec; `vring.h` | S–M | ★★★ | `kernel/virtio/` + `blockdev`/`net` |
| 18 | TPM 1.2/2.0 (TIS/CRB) | TCG TIS 1.3; PTP 2.0; QEMU tpm docs | M | ★★★★ | `kernel/guard/` + ACPI TPM2 table |
| 19 | RNG (RDRAND/RDSEED, virtio-rng) | Intel SDM vol.1; virtio spec | S | ★★★★★ | `kernel/arch/x86/` + `kernel/virtio/vring.h` |
| 20 | ACPI S5 shutdown (PM1) | ACPI spec + FADT + `_S5` | S | ★★★★ | `kernel/power.cc`, `acpi.cc` |
| 21 | ACPI S3 suspend | ACPI spec, FADT, AML (`_S3/_WAK`) | XL | ★★ | `kernel/power.cc`, `acpi.cc`, `sched` |
| 22 | CPU C-states (MWAIT) | ACPI `_CST`; Intel SDM MWAIT | M | ★★★ | `kernel/sched` idle + `arch/x86` |
| 23 | cpufreq (PERF_CTL/HWP) | Intel SDM MSRs 0x199/0x774 | M | ★★ | `kernel/arch/x86/cpuid` |
| 24 | hwmon (coretemp/k10temp) | Intel SDM MSR 0x19C; AMD BKDG | M | ★★★ | `kernel/chips/` + serial/ui |
| 25 | ACPI thermal zones/battery | ACPI spec `_TMP/_BIF/_BST`, SBS | M (needs AML) | ★★ | `kernel/acpi` (gated on mini-AML) |
| 26 | Watchdog (iTCO, virtio-wd) | Intel ICH datasheet; virtio spec | S–M | ★★★ | new `kernel/watchdog`, `pci` IDs |
| 27 | Synaptics PS/2 touchpad | Synaptics interfacing guide (mirror) | S–M | ★★★★ | `kernel/input/msaux` sibling |
| 28 | HID gamepad/wacom | HID 1.11 + Usage Tables | M | ★★★ | `kernel/usb` int-IN + `kernel/input` |
| 29 | exFAT | Microsoft exFAT spec (2019) | M | ★★★★ | `kernel/vfs/` |
| 30 | ext2 read | "Inside ext2" docs; Linux fs/ext2 | M | ★★★★ | `kernel/vfs/` |
| 31 | NTFS read | ntfsdoc; Linux ntfs3 | L | ★★★ | `kernel/vfs/` |
| 32 | UDF | ECMA-167 + OSTA | L | ★ | `kernel/vfs/` |
| 33 | WiFi (ath9k realistic; iwlwifi brutal) | Linux ath9k/iwlwifi; linux-firmware | XL | ★★★ | `kernel/net` + 802.11 stack (missing) |
| 34 | Bluetooth HCI | BT Core vol.4; USB transport | L | ★★ | `kernel/usb` + new `bt` stack |
| 35 | Intel GPU blitter/compute | Intel OSRC PRMs (SKL vol 6/7/12) | L–XL | ★★★★ | `kernel/xdp` `XDP_CAP_2D/COMPUTE` |
| 36 | AMD/NVIDIA GPU | amdgpu source; nouveau RE | XL | ★ | `kernel/xdp` — skip |
| 37 | Floppy FDC | osdev.wiki FDC | — | 0 | **intentionally excluded** |
| 38 | PCI serial cards / 16550 depth | PCI class 07; 16550 datasheet | S | ★★ | `kernel/serial.cc`, `pci` |
| 39 | Parallel port SPP | IEEE 1284 | S | ★ | `kernel/chips` or serial-ish util |
| 40 | Intel TXT / SGX | Intel SDM / SGX docs | XL | 0 | excluded — ecosystem dead/hostile to hobby OS |
| 41 | virtio-gpu depth/1.0 | virtio-gpu spec | M | ★★★ | `kernel/xdp/backend_virtgpu.cc` |
| 42 | AMD GPU compute / NVIDIA GSP | — | XL | ★ | skip (see §7) |

## 10. Recommended top-10 (ordered by value ÷ effort, real-hardware impact)

1. **RNG — RDRAND/RDSEED + virtio-rng** *(S, `arch/x86` + `virtio/vring.h`)*
   — half a day, feeds everything: crypto, DHCP XIDs, seed material.
2. **USB mass storage BOT** *(M, `usb` → `blockdev`)* — lets real users boot
   / load payloads off a USB stick; the single biggest "real PC" unlock.
3. **USB hub class** *(M, `xhci.cc`)* — without it real machines see only
   root ports; prerequisite for items 6–8 being usable at all.
4. **Intel e1000 then e1000e** *(M, `kernel/net`)* — real wired networking on
   actual silicon; e1000 lands the ring machinery, e1000e reuses it.
5. **ACPI S5 shutdown via FADT PM1** *(S, `power.cc`)* — power-off works on
   real hardware, not only QEMU.
6. **SDHCI (SD + eMMC)** *(L, `blockdev`)* — sole storage on cheap laptops/
   embedded boards; the register spec is public and well-shaped.
7. **USB CDC-ACM serial** *(S–M, `serial.cc`/`usb`)* — consoles, bootloaders,
   dev boards; tiny spec, high debug value.
8. **Synaptics PS/2 touchpad** *(S–M, `input/msaux`)* — the difference
   between "keyboard-only demo" and "usable laptop OS".
9. **SATA NCQ + multi-port + hotplug** *(M, `ahci.cc`/`blockdev`)* —
   real-world performance and the multi-disk paths blockdev needs anyway.
10. **TPM 2.0 (TIS+CRB) minimal** *(M, `guard/`)* — `GetRandom`, PCR read/
    extend, sealed keys — small, self-contained, high-cred security story.

*Runners-up:* RTL8169 (covers the other half of real NICs), USB Audio UAC1
(real headsets — gated on xHCI isochronous support), exFAT read (the FS on
every modern flash drive), virtio-1.0 modern transport (future-proofing all
virtio), mini-AML evaluator (unblocks S3/thermal/battery — biggest unlock
ratio in the power cluster, but its own project).
