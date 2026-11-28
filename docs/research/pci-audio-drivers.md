# PCI Audio Driver Research

Research notes for implementing real PCI audio hardware drivers in the
bare-metal kernel. Existing code: `kernel/audio/hda.cc` (Intel HDA),
`kernel/audio/audioac97.cc` (generic AC97/ICH), `kernel/audio/audioes1370.cc`
(Ensoniq ES1370/1371), `kernel/audio/audioisa.cc` + `sb16.cc` (ISA SB16),
`kernel/audio/pullmix.cc` (software mixer), `kernel/audio/audiopci.cc`
(PCI probe + `AudioChipList` classification).

Primary sources: ALSA `sound/pci/*` (torvalds/linux on git.kernel.org /
github.com), `include/linux/pci_ids.h`, `include/sound/emu10k1.h`,
pci-ids.ucw.cz, osdev.wiki (AC97, SB16), chipset datasheets.

All chips below are classic PCI (PCI IDs are `vendor:device` hex). None of
the "consumer" chips need a signed or redistributable firmware blob for
basic stereo playback unless explicitly flagged; the pro/DSP cards are the
firmware problem children.

Conventions:

- "IO BAR" = PCI BAR decoded as I/O port space (`inb/outb/inl/outl`).
- "MMIO BAR" = memory-mapped BAR (`readl/writel` on a mapped VA).
- DMA is always bus-master DMA into host RAM, 32-bit physical addresses,
  unless noted. Our `pmm_allocate_page[s]()` physical addresses work.
- Effort: S < one weekend, M = a few days, L = a week+.
- Value: usefulness for actually hearing sound on real hardware / QEMU.

---

## 1. Generic AC'97 (ICH NAM/NABM model) — already have `audioac97.cc`

PCI class 0x04 subclass 0x01 = AC'97 controller. BAR0 = NAM (Native Audio
Mixer, IO), BAR1 = NABM (Native Audio Bus Master, IO). DMA via a
32-entry buffer-descriptor list (BDL) per channel; `BDBAR` register,
`CIV/LVI/PICB/PIV` position registers, `CR` run bit (`RPBM`) + `RR` reset.

Our `audioac97.cc` implements: BAR claim, `GLOB_CNT` cold + warm reset
retry, `PRIRD` codec-ready poll, NAM reset + powerdown regs, front-DAC
rate, PCM-out channel with 32 BDs, `ac97_pos()` from CIV+PICB, master +
PCM volume.

Gaps vs the AC'97 spec (worth fixing in place, tiny effort):

- **Cold/warm reset semantics**: `GLOB_CNT` bit 1 = cold reset, bit 2 =
  warm reset; we write 0 then 0x02 — correct — but the ICH spec also wants
  the *write* of 0x0002 to be followed by reading `GLOB_STA` until
  `PRIRDY` with the option of a second warm reset *only* (0x0004). Our
  `cnt_cold_reset | cnt_warm_reset` fallback (0x06) is what ALSA's
  `intel8x0` does too, fine.
- **PCM record (PCM_IN) channel**: entirely absent — only PCM_OUT BD
  registers are wired. Capture needs the PI channel block (NABM+0x00).
- **MIC_IN channel** (NABM+0x20): absent.
- **Slots/registers not touched**: `GLOB_STA` bits for secondary
  codecs (SCR `0x02000000`, SD2 `0x04000000`), SRI/SCI capability bits;
  VRA (variable rate audio) enable bit in `GLOB_CNT` bit 0x00000001 — we
  never set `CNT_ST`/`CNT_VRA`, so codecs that require explicit VRA
  enable may ignore `nam_front_dac_rate`.
- **Volume regs**: we only write `nam_master_volume` (0x02) and
  `nam_pcm_volume` (0x18). Missing: `nam_aux_out` (0x04), mic gain
  (0x0E), record select (0x1A), record gain (0x1C), `nam_pcm_center`/
  `nam_pcm_surround`/`nam_pcm_lfe` (0x36/0x38/0x3A on AC'97 2.x), and the
  extended-capability regs 0x28/0x2A (VRA/DAC slots/SPDIF).
- **DAC slots**: for multichannel AC'97 the front/surr/LFE rate regs are
  0x2C/0x2E/0x30 and ADC rate is 0x32 — we only program 0x2C.
- **Interrupts**: we poll CIV; the IOC bit in BD flags is set but the
  NABM interrupt status/enable registers are never used. Fine for now,
  but a shared "BDL" abstraction should include an optional IRQ path.
- **Secondary codecs**: `GLOB_CNT` ASCR/ASE bits to wake codec #2.
- **Per-vendor deviations**: SiS 7012 has `pos_shift` differences
  (CIV/PICB meaning differs, ALSA `intel8x0.c` uses `pos_shift`); VIA
  8233+ is NOT NAM/NABM (see §9); Intel ICH4+ adds `NAMBAR`/`NABMBAR`
  remap regs in PCI config space (offset 0x44); nForce uses MMIO BARs
  instead of IO BARs.

Relevant PCI IDs (ALSA `intel8x0.c`):

| IDs | chip |
|---|---|
| 8086:2415 2425 2445 2485 24c5 24d5 25a6 266e 27de 2698 7195 | ICH0..ESB2, 440MX |
| 1022:7445 746d | AMD 768/8111 (intel8x0 compatible) |
| 10b9:5455 | ALi M5455 (DEVICE_ALI quirks) |
| 10de:01b1 003a 006a 0059 008a 00da 00ea 026b | nForce1..MCP51 (MMIO BARs) |
| 1039:7012 | SiS SI7012 (see §9) |

**Effort: S (done; hardening also S). Value: already the QEMU default
(`-device intel-hda`? no — `-soundhw ac97` / `-device intel82801aa` /
`ich9-intel-hda` is HDA; QEMU's `AC97` device is 82801AA = 8086:2415).**

---

## 2. Creative EMU10K1 / CA0102/CA0108 — SB Live! / Audigy family

ALSA: `sound/pci/emu10k1/` (`emu10k1_main.c`, `include/sound/emu10k1.h`,
`emu10k1.h` reg file, `emupcm.c`, `emumixer.c`, `p16v.c`).

### PCI IDs

| ID | chip / card |
|---|---|
| 1102:0002 | EMU10K1 — SB Live! (all SBxxxx subsystems; subsystem picks card model) |
| 1102:0004 | EMU10K2/CA0102/CA0108 — Audigy, Audigy 2, Audigy 4, Audigy 2 Value |
| 1102:0008 | EMU10K2.5 — Audigy 2 ZS / Audigy 2 Value variants, EMU0404/1010 w/ fw |
| 1102:0003/0009 etc. | gameport function (func 1), ignore |
| 1102:4001 | E-mu audio dock FPGA (function of 0008/0004 cards) |

Subsystem-vendor is always 1102; ~40 subsystem IDs pick mixer models.

### Interface

- BAR0 = IO port block, 32 bytes used.
- `PTR` (0x00) / `DATA` (0x04): **indexed register file** — write
  `(chn | (reg << 16))` to PTR, read/write DATA. ~0x80 globals +
  per-channel (64 voice) regs.
- `IPR` (0x08) pending, `INTE` (0x0c) enable. `WC` wall clock (0x10),
  `HCFG` (0x14), MPU401 MUDATA/MUCMD (0x18/0x19), `AC97DATA` (0x1c),
  `AC97ADDRESS` (0x1e, bit7 = codec ready).
- Audigy 2/4 adds `PTR2/DATA2/IPR2/INTE2` at 0x20-0x2c (P16V engine) and
  `HCFG2` (0x34). `A_GPIO`/`A_IOCFG` at 0x18 (Audigy) vs 10k1 `HCFG`
  GPIO bits.
- AC'97 codec write: write reg addr to `AC97ADDRESS`, data to
  `AC97DATA`; wait for `AC97ADDRESS & 0x80` before use. On Audigy the
  same port talks to STAC9721/CS codecs; some cards have a second codec.

### DMA model

- **Host page-table DMA**: driver builds a page table in host RAM;
  `PTB` (indexed reg 0x40) = phys base (4K-aligned). Each of 64 voices
  maps its buffer via `MAPA`/`MAPB` (indexed 0x0c/0x0d) → two page-table
  entries each; a voice's `CSBA` (current start-block addr) and
  `CCCA` (current control) drive playback from the mapped region.
  `MAP_PTI_MASK0/1` differ for 4K vs 8K "address mode" pages
  (`HCFG_EXPANDED_MEM`).
- A dedicated **silent page** (zero-filled host page) is mapped into
  every voice's two PTEs at init — the standard trick we should copy.
- `TCB`/`TCBS` = tank (TRAM) cache base/size for effects/delay memory.
- Capture: `ADCBA`/`ADCBS` (0x46/0x48), `MICBA`/`MICBS`, `FXBA`/`FXBS`
  indexed regs — simple linear-buffer record engines.
- Quirk: CA0102 reads past the buffer end — ALSA pads allocations by a
  page (`snd_emu10k1_detect_iommu` workaround comment documents it).

### Voice programming (minimal playback path)

1. `snd_emu10k1_init`: `HCFG = LOCKSOUNDCACHE|LOCKTANKCACHE|MUTEBUTTON`,
   `INTE = 0`, reset MIC/FX/ADC buffer regs, init all 64 voices
   (`DCYSUSV=0` = voice disabled, `VTFT`, `CVCF` filter-current max,
   `DSL=0x10`, `FXRT=0x32100000` send routing).
2. `PTB = ptb_phys`, `TCB=0`, `TCBS=256K`, map silent page into MAPA/B.
3. `HCFG |= AUDIOENABLE` (+ AUTOMUTE; on Audigy also GPOUT0 via A_IOCFG
   to enable analog out; `HCFG_AC3ENABLE_*` for AC3 passthrough).
4. Per voice: write host pages into page table → `MAPA/MAPB`,
   `PSST`/`DSL` (start/loop-end addresses in "audio samples" units —
   interleaved stereo counts), `CCCA` (format bits, stereo),
   `CSBA = 0`, pitch regs `IP`/`PITCH` (48k default: `IP=0`, `PITCH`
   init `0x0000ffff`), `DCYSUSV = 0x7f7f0000`-style "sustain full +
   channel enable", `PTRX`/`CPF` sends.
5. IRQ: `IPR_CHANNELLOOP` per-voice (half-loop + loop-end), `CLIEL/H`
   (channel loop int enable low/high 32+32), `SOLEL/H` stop-on-loop-end.

### Mixer

All mixing happens inside the FX DSP "GPR" regs via AC'97 volume regs for
the analog path; minimal stereo only needs AC97 master (0x02) + PCM
(0x18) on the codec.

### Quirks

- `enable_ir` GPIO dance at init (IR remote receiver).
- E-mu 1010/1212/1820: **needs firmware blobs** `emu/hana.fw`,
  `emu/emu1010b.fw`, `emu/audio_dock.fw`, `emu/micro_dock.fw`,
  `emu/emu0404.fw`, `emu/emu1010_notebook.fw` — FPGA config for dock.
  Normal SB Live!/Audigy do NOT need them.
- Audigy 2 ZS Notebook: extra SPI DAC (WM8768/WM8568) and I2C ADC
  (WM8775) init arrays — subsystem quirk.
- AC'97 slot enables: `AC97SLOT` indexed reg 0x5f (REAR_LEFT/RIGHT = 0x03,
  CNTR=0x10, LFE=0x20) — enable extra output slots for 4/6 ch.

**Effort: L** (indexed reg model is easy; the page-table + per-voice
programming is the bulk; FX/mixer can be skipped initially).
**Value: high on real hardware** (huge installed base of PCI-era cards),
**zero under QEMU** (no emulation exists). Rate: **L / M-high**.

---

## 3. Creative CA0106 — SB Live! 24-bit / Audigy SE / Audigy LS / SB 5.1vx

ALSA: `sound/pci/ca0106/` (`ca0106_main.c`, `ca0106.h`).

### PCI ID

| ID | chip |
|---|---|
| 1102:0007 | CA0106-DAT "P17" — all models, subsystem picks: 1002/1005 AudigyLS SB0310, 1006/1007 Live24 SB0410/13, 100a/1011 Audigy SE SB0570, 1012/1013 X-Fi Xtreme Audio (CA0106 in disguise!), 1004 SB1070 |

### Interface

- BAR0 = IO port block.
- Same PTR (0x00)/DATA (0x04)/IPR (0x08)/INTE (0x0c) header as emu10k1
  but a *different* indexed register file (P17): channel regs are
  `SCS`/`CSBA`-like indexed entries; `CA0106_HCFG` config reg,
  `BASIC_INTERRUPT`/`EXTENDED_INT_MASK` (indexed 0x?? per ca0106.h),
  `SCS0..3` sample-control status per DMA channel.
- DMA: 4 fixed "audio" DMA engines, each with a **period table in
  card-local RAM** — the driver allocates 1 KiB DMA page and programs
  period (64 B min granularity, `period_size = n*64` constraint).
  Rates: 48/96/192 kHz only. Formats S16_LE/S32_LE.
- Codec: **no universal codec**. Three flavors by subsystem:
  - `ac97=1` → STAC9721 AC'97 accessed via the P17 AC97 indexed regs
    (PTR-based, same AC97DATA-style protocol).
  - `gpio_type=1` + `i2c_adc` → WM8775 ADC over I2C bitbang + CS4382/
    WM8768 DAC over SPI (`IPR_SPI`/`IPR_I2C_DAC` completion bits).
  - `spi_dac` → WM8768GEDS/CS4382 via SPI register writes.
- Init essence: `INTE = 0x105` (ALSA; Windows uses 0x1e0),
  `HCFG = AC97|AUDIOENABLE` (0x00001409 family bits — comment warns
  0x1000 breaks AC3), SPI/I2C codec arrays, `BASIC_INTERRUPT` enable.
- IRQ: `IPR_AI` (audio pending, PTR reg 0x76 tells which channel),
  per-channel loop interrupts via EXTENDED_INT_MASK.
- Quirks: capture routes analog→AC97→ADC chip (weird feedback path);
  SB0410 has NO AC97 (I2C/SPI only); MSI/Shuttle/Gigabyte onboard
  variants have their own GPIO wiring tables.

**Effort: M** (share PTR/DATA helper with emu10k1; simplest model is
  "4 DMA engines + one DAC"). **Value: medium** — these cards are
  common, and "X-Fi Xtreme Audio" is mislabeled CA0106 so it looks like
  a cheap way to claim X-Fi support. **M / medium.**

---

## 4. Creative ctxfi — EMU20K1/EMU20K2 "X-Fi" (XtremeMusic/ Titanium/HD)

ALSA: `sound/pci/ctxfi/` (`xfi.c`, `ctatc.c`, `cthw20k1.c`, `cthw20k2.c`,
`ct20k1reg.h`).

### PCI IDs

| ID | chip |
|---|---|
| 1102:0005 | EMU20K1 — X-Fi XtremeMusic/Elite Pro/Fatal1ty (PCI) |
| 1102:000b | EMU20K2 — X-Fi Titanium / Titanium HD / XtremeGamer (PCIe) |

### Interface

- MMIO, big register window. **20K1: BAR0** (some cards present a
  "UAA" USB-audio-class persona first — Mode Change Register at
  `UAA_CORE_CHANGE`, magic dwords `CTL-`/`CTLF`/`CTLi`/`CTLA`/`CTLZ`/`CTLL`
  tell current mode; `uaa_to_xfi()` switches persona, then use BAR5).
  **20K2: BAR2**; clear `GCTL_UAA` bit in `GLOBAL_CNTL_GCTL` to leave
  UAA mode.
- No firmware blob: DSP program is loaded via register writes; all
  routing tables are in the driver.
- Register families (ct20k1reg.h): `SRCCTL/SRCCCR` (sample-rate
  converters), `SRTCTL`, `AMOPLO/AMOPHI` (audio mixer operand ptrs),
  `TPTR` (transport pointers for PCM DMA), `GPIO/INTCTL`, DAIO/I2S.
- DMA: **TPR (transport) rings** — each stream gets a transport pointer
  block (TPTR) with a host buffer address + period settings; card pulls
  samples. Up to 8× playback + capture daio devices.
- IRQ: `INTCTL` interrupt controller regs; per-stream pending flags.
- Bring-up essence: leave UAA mode → hw reset (`GCTL` writes) → init
  SRC/rSRC managers (resource allocator: each stream needs SRC, mixer
  input, audio-ring entries) → set up TPTR buffers → arm `INTCTL` →
  route DAIO front out.
- Quirks: model table `CT20K1`, `CTSB073x`, `CTHENDRIX`, `CTUAA`,
  `CT20K2` etc.; Titanium vs XtremeMusic need different GPIO/DAC setup
  (CS4382/WM8775 companion chips on some).

**Effort: L** — resource manager + TPTR + SRC programming, biggest
consumer-chip surface in this list. **Value: low-medium** (nice-to-have
flagship; few real cards). **L / low.**

---

## 5. Cirrus Logic Sound Fusion CS46xx — CS4280/CS4612/CS4614/CS4622/CS4624/CS4630

ALSA: `sound/pci/cs46xx/` (`cs46xx.c`, `cs46xx_lib.c`, `cs46xx.h`).

### PCI IDs

| ID | chip |
|---|---|
| 1013:6001 | CS4280 |
| 1013:6003 | CS4612/CS4614/CS4622/CS4624/CS4630 (CrystalClear SoundFusion) |
| 1013:6004 | CS4615/CS4281-family (CS4281 has own driver, 1013:6005 `cs4281.c`) |

### Interface

- **BAR0 = "BA0"** MMIO control window (~4KB): `BA0_HISR` (host irq
  status, 0x00), `BA0_HSR0` (0x04), `BA0_HICR` (int ctrl 0x08),
  `BA0_DMSR` (0x100), host DMA regs `BA0_HSAR/HDAR/HDMR/HDCR`
  (0x110-0x11c), PCI-config shadow regs 0x300+, `BA0_CLKCR1/2`, `PLLM`
  (PLL control ~0x400+), `BA0_SERMC` (serial EEPROM), `BA0_FLSR` flash.
- **BAR1 = "BA1"** MMIO DSP program/data memory window (~1-4MB): DSP
  code/data lives here; you write DSP tasks + parameter buffers
  (C-written-by-host DSP "modules": `CS46XX_DSP_MODULES` — spp:
  `mix_spdifout`, `pcmout`, `record`...). `BA1_SPCR` (SP control),
  `BA1_DREG` debug, `BA1_CCFG`, `BA1_SSTS` (serial port status),
  `BA1_PMEM` program memory, `BA1_DMEM` data memory.
- AC'97 access: through **serial port regs in BA1 space** —
  `ACCAD` (0x46c = command address), `ACCDA` (0x470 = command data),
  `ACCTL`/`ACSTS` command/status; write addr+data, poll `ACSTS_CRDY`.
  Codec reset via `ACCTL` `RSTN` bit (ARST# active low: write 0 to
  reset, 1 to release).
- DMA engines: two independent engines — (a) **host DMA (HISR/HDAR)**
  for control/messages; (b) **DSP serial-port DMA**: audio data is
  fetched by the DSP itself via "task" descriptors placed in BA1 memory
  — each playback stream = a DSP task with its own buffer chain. This
  is "dual-engine" in the brief; PCM data never goes through a classic
  BDL — the DSP reads host memory via the "BA1 host memory access"
  window (`BA1_HBAR`/`BA1_HCAR`).
- DSP firmware: **old-DSP path** (`cs46xx_image.h`, `cs46xx_dsp_task.h`)
  has built-in microcode tables compiled into the driver — works
  offline. **new-DSP path** (`CONFIG_SND_CS46XX_NEW_DSP`) needs
  `cs46xx/ba1` firmware blob = serialized BA1 memory image
  (`struct ba1_struct` ~120KB, `request_firmware`). We would implement
  the OLD DSP path (image is in-tree in ALSA, can be transcribed).
- Init essence: `BA0_HISR` clear; clock/PLL setup (`CLKCR1`, `PLLM`);
  download DSP code/param/image memory into BA1 window (or in-tree
  image); `BA1_SPCR = RSTSP` then `RUN|RUNFR|DRQEN`; create DSP tasks
  (mono/stereo PCM, SPDIF) via symbol offsets in image; IRQ via
  `BA0_HISR`/`HICR` with `mid-fi` "period tick" from DSP.
- Quirks: ThinkPad CLKRUN (PCI power gating — `thinkpad` param),
  `external_amp` GPIO, `mmap_valid` for direct-mapped BA1, the
  "metallic sibilant" reload note in driver header.

**Effort: L** (DSP task model + big image table). **Value: medium** —
distinctive in older laptops (ThinkPad T21/A22), but every unit is
also AC97 at heart... it is NOT — the codec is generic AC97 but the
engine is custom. **L / medium-low.**

---

## 6. Trident 4DWave DX/NX (+ SiS SI7018)

ALSA: `sound/pci/trident/` (`trident.c`, `trident_main.c`, `trident.h`).

### PCI IDs

| ID | chip |
|---|---|
| 1023:2000 | 4DWave DX |
| 1023:2001 | 4DWave NX |
| 1039:7018 | SiS SI7018 (same engine, southbridge-integrated) |

### Interface

- **BAR0 = IO**, ~0x100 bytes. Classic "channel registers + bank
  registers" model shared with ALi5451 (see §10):
  - Per-channel window regs (write channel-select via `CH_START`
    area): `CH_DX_CSO_ALPHA_FMS` 0xe0, `CH_LBA` 0xe4 (linear buffer
    base phys), `CH_DX_ESO_DELTA`/`CH_NX_DELTA_CSO` 0xe8,
    `CH_DX_FMC_RVOL_CVOL`/`CH_NX_ALPHA_FMS_FMC_RVOL_CVOL` 0xec,
    `CH_GVSEL_PAN_VOL_CTRL_EC` 0xf0, `CH_EBUF1` 0xf4, `CH_EBUF2` 0xf8.
  - Bank A control: `T4D_START_A` 0x80, `T4D_STOP_A` 0x84, `T4D_DLY_A`
    0x88, `T4D_SIGN_CSO_A` 0x8c, `T4D_CSPF_A` 0x90 (current sample
    position frame — readable position!), `T4D_CEBC_A` 0x94,
    `T4D_AINT_A`/`AINTEN_A` 0x98/0x9c, `T4D_LFO_GC_CIR` 0xa0,
    `T4D_MUSICVOL_WAVEVOL` 0xa8, `T4D_SBDELTA_DELTA_R` 0xac,
    `T4D_MISCINT` 0xb0; bank B mirrors at 0xb4+ (`CSPF_B` 0xbc).
  - AC'97 via `DX_ACR0_AC97_W` 0x40, `DX_ACR1_AC97_R` 0x44,
    `DX_ACR2_AC97_COM_STAT` 0x48 (command/status; NX uses same).
  - MPU401 at 0x20-0x23, gameport 0x30-0x3c, NX SP/DIF regs
    `NX_SPCTRL_SPCSO` 0x24, `NX_SPLBA` 0x28, `NX_SPESO` 0x2c,
    `NX_SPCSTATUS` 0x64, `NX_TLBC` 0x6c (TLB control).
- DMA: up to 64 playback voices in two banks (A/B); each voice has
  LBA (linear base addr, phys), ESO (end sample offset), Delta (rate
  pitch = (rate*488)/44100 style fractional), CSO (current sample
  offset = position), FMC/RVOL/CVOL (format + pan + volume), EBUF1/2
  (extra buffers for wavetable/foldback). Write the *channel regs
  through a shared window*: `OUTW(channel|start, 0xe0)` style — Trident
  uses a "channel address" at the channel regs (byte offset =
  channel * stride... actually the channel regs are indexed by writing
  to `CH_START`-windowed ports).
- Interrupts: `T4D_AINT_A`/`AINTEN_A` per-channel EOL/mid-point;
  `MISCINT` global status; legacy regs 0x00-0x0f (DMA emulation),
  `T4D_STIMER` 0xc8 timer.
- Codec: standard AC97 via ACR regs, NX supports 2nd codec (surround).
- Bring-up: reset channel regs, AC97 reset via ACR2, allocate voice in
  bank, program LBA/ESO/Delta/FMC, set `START_A` bit, poll `CSPF_A`.
- Quirks: DX vs NX differ in the meaning of the 0xe0/0xe8 packed fields
  (`CH_DX_*` vs `CH_NX_*`); SiS7018 shares the engine but folds banks
  differently (`snd_trident_create(..., 1or2 banks, ...)`); foldback
  PCM on DX/NX; wavetable 8MB TLB (`NX_TLBC`).

**Effort: M** — the channel-window IO model is the fiddliest part but
the register set is small. **Value: medium** — common in old
laptops/boards and pairs with our existing AC97 codec code.
**M / medium.**

---

## 7. Yamaha YMF724/740/744/754 (DS-1)

ALSA: `sound/pci/ymfpci/` (`ymfpci.c`, `ymfpci.h`, `ymfpci_main.c`).

### PCI IDs

| ID | chip |
|---|---|
| 1073:0004 | YMF724 |
| 1073:000d | YMF724F |
| 1073:000a | YMF740 |
| 1073:000c | YMF740C |
| 1073:0010 | YMF744 |
| 1073:0012 | YMF754 |

### Interface

- **BAR0 = MMIO** register window (8KB), `YMFREG(chip, reg)` helpers;
  BAR1/BAR2 are legacy FM/MPU/joystick IO BARs on 744/754.
- Key regs (ymfpci.h): `YDSXGR_INTFLAG` 0x0004, `ACTIVITY` 0x0006,
  `GLOBALCTRL` 0x0008, `TIMERCTRL` 0x0010, `SPDFOUTCTRL` 0x0018,
  `EEPROMCTRL` 0x0020, `DSPPROGRAMDL` 0x0048, `DLCNTRL` 0x004c,
  GPIO block 0x50-0x5a, `AC97CMDDATA` 0x0060, `AC97CMDADR` 0x0062,
  `PRISTATUSDATA/ADR` 0x64/0x66, `SECSTATUSDATA/ADR` 0x68/0x6a,
  `SECCONFIG` 0x0070, volume regs 0x80-0xa6 (legacy/native DAC/loop),
  play/record control banks at 0x0100+ (`YDSXGR_PLAYCTRL_VOLUME`…),
  `MAPOFEFFECT`, effect bank regs.
- DMA: **hardware voice banks** — each playback voice = a "bank" of
  regs (buffer phys base, format, rate, volume, delta, position,
  loop-flag). `YDSXG_PLAYBACK_VOICES` (32) + `YDSXG_EFFECT_VOICES`.
  Host writes the bank, chip fetches samples; position is readable
  per voice. Similar granularity to trident — no BDL, no page table.
- Codec: AC97 via `AC97CMDADR/AC97CMDDATA`; write addr to 0x62, data
  to 0x60, wait busy; `PRISTATUS*` for read.
- **Firmware required**: `yamaha/ds1_dsp.fw` (DSP microcode,
  `YDSXGR_DSPPROGRAMDL` upload port at 0x48) AND
  `yamaha/ds1e_ctrl.fw`/`ds1_ctrl.fw` (controller microcode, name
  chosen by chip revision: `ds1e` for 724F/740C+, `ds1` for 724/740).
  From `snd_ymfpci_request_firmware()` in ymfpci_main.c. **This is a
  blocker for us** unless we extract the blobs — the card will not
  play without the DSP image.
- Legacy mode: `PCIR_DSXG_LEGACY`/`ELEGACY` PCI config registers
  relocate SB16/MPU401/FM/joystick decode — handy notes if we ever want
  SB16 compat without ISA.
- IRQ: `YDSXGR_INTFLAG` global; per-voice interrupts via effect/
  play intflag regs; timer tick 0x10/0x12.

**Effort: M** (bank regs are simple) but **firmware-blocked** →
**Value: low for us** unless we can embed the ~30KB DSP blobs.
**M / low (firmware).**

---

## 8. Compat note row: VIA82xx / ICE1712 / Oxygen / SiS7012 / NM256 / ES1370

(Covered individually in §9/§10 where relevant; ES1370 = done.)

### VIA VT82xx (southbridge audio)

ALSA `sound/pci/via82xx.c`.

| ID | chip |
|---|---|
| 1106:3058 | VT82C686A/B/C (rev 0x10-0x20) — "686A" |
| 1106:3059 | VT8233/8233C/8233A/8235/8237/8251 (revision-reg selects: 0x10 pre, 0x20 8233C, 0x30 8233, 0x40 8233A, 0x50 8235, 0x60 8237, 0x70 8251) |
| 1106:3068 | MC97 modem function — ignore |

- **BAR0 = IO** (~256B). NOT NAM/NABM. Two generations:
  - **686x**: three SGD (scatter-gather DMA) channels at offsets
    0x00 (playback), 0x10 (capture), 0x20 (FM): per-channel regs
    `STATUS` (+0x00), `CONTROL` (+0x01, START/TERMINATE/PAUSE +
    INT_FLAG/INT_EOL/AUTOSTART), `TYPE` (+0x02, 16BIT/STEREO/INT_*),
    `TABLE_PTR` (+0x04 phys of SG table), `STOP_IDX` (+0x08: stop index
    + 16bit/stereo + sample-rate field), `CURR_PTR/COUNT/INDEX`.
    SG table entries: phys-addr|FLAG(0x40000000)|EOL(0x80000000) +
    byte-count word.
  - **8233+**: playback moves to `MULTPLAY` block at 0x40, capture at
    0x60; additional `PLAYBACK_VOLUME_L/R` byte regs at channel+2/3
    (DXS volume, 0-31); `MULTPLAY_FORMAT` (channels<<4 | 0x80=16bit);
    `CAPTURE_FIFO_ENABLE` bit6 at cap+2; `CAPTURE_CHANNEL` 0x63
    (mic/line select); `SGD_SHADOW` 0x84 combined status.
- AC'97 via `VIA_REG_AC97` 0x80 (dword): codec-id bits30-31, READ bit23,
  cmd<<16 | data; poll BUSY bit24; `PRIMARY/SECONDARY_VALID` bits.
- AC-link init via **PCI config regs**: `VIA_ACLINK_STAT` 0x40
  (ready bits C00/C01/C10/C11) / `VIA_ACLINK_CTRL` 0x41
  (ENABLE|RESET|SYNC|SDO|VRA|PCM|FM|SB).
- IRQ: per-channel FLAG/EOL/STOP bits in STATUS (W1C), shadow at 0x84.
- Quirks: `dxs_support` modes (DXS = direct execution sys channels on
  8233+), `nodelay` 500ms codec settle, `ac97_clock`, table-of-quirks
  by subvendor (ASRock etc.), `VIA_TBL_BIT` terminator semantics.

**Effort: S-M** — 686x SGD model is as easy as ES1370; 8233 adds
channel-block juggling. **Value: HIGH** — ubiquitous in old boards,
and **QEMU does not emulate it** but real test hardware is everywhere.
**S / high.**

### ICEnsemble ICE1712 (Envy24)

ALSA `sound/pci/ice1712/` (`ice1712.c` core + `delta.h`, `ews.h`,
`hoontech.h` board files).

| ID | chip |
|---|---|
| 1412:1712 | ICE1712 Envy24 (all M-Audio Delta 44/66/1010, Terratec EWS88/DMX6Fire, Hoontech, EZ8...) |
| 1412:1724 | VT1724 Envy24HT/PT — separate `ice1724.c` driver |

- **BAR0 = IO control block** (32B): `ICEREG(ice, x)` — AC97_CMD/INDEX/
  DATA, `ICE1712_SYS_STATUS`, `MONITOR_ROUTECTRL`, `GPIO` window
  (IREG_GPIO_* via index/data indirection at `ICEREG(ice, INDEX/DATA)`).
- **BAR1 = IO multitrack (ICEMT)** (64B): pro PCM regs — `RATE`,
  `AC97_CMD`, `MONITOR_*`, `DOA`, `DID`, DMA burst control.
- DMA: **DS-channel descriptors** — `ICEDS INDEX`/`DATA` port pair:
  write `(channel<<4)|reg` to INDEX, dword to DATA. 16+ channels of
  linear-buffer DMA with phys base/size; consumer path is AC97, pro
  path is the multitrack engine (Delta 66 etc.).
- Codec: AC97 via ICEREG AC97_CMD/INDEX/DATA triple + ready bit;
  CS8427 SPDIF transceiver over **I2C bitbang through GPIO**.
- Per-card mixer = GPIO bit twiddling (different codec wiring per
  board — that's why ALSA has delta/ews/hoontech board files).
- IRQ: INT_STATUS byte regs; MT/consumer/FIFO bits.

**Effort: M** (board matrix is the work; generic consumer path is S).
**Value: medium-high** — Delta cards are *the* classic Linux audio
cards and the base engine is small. **M / medium.**

### C-Media Oxygen (CMI8788) — AV series / Xonar

ALSA `sound/pci/oxygen/` (`oxygen.c`, `oxygen.h`, `oxygen_regs.h`).

| ID | chip |
|---|---|
| 13f6:8788 | CMI8788 "Oxygen HD" — C-Media ref, AuzenTech X-Meridian, HT-Omega Claro/eClaro, TempoTec HiFier, ASUS Xonar DG/DGX/Essence... (subsystem distinguishes ~20 models) |

- **BAR0 = IO**, 0x100 bytes (`OXYGEN_IO_SIZE`). Dead-simple DMA:
  per-stream `{ADDRESS, COUNT, TCOUNT}` triples at
  0x00/0x08/0x10/0x18/0x20/0x30 (channels A, B, C, SPDIF, MULTICH,
  AC97); `OXYGEN_DMA_STATUS` 0x40 run bits (`OXYGEN_CHANNEL_*`),
  `DMA_PAUSE` 0x41, `DMA_RESET` 0x42, `PLAY_CHANNELS` 0x43 (2/4/6/8ch +
  burst size), `INTERRUPT_MASK` 0x44.
- Codecs: **SPI** (AK4396 DACs, WM8785 ADC, CS4362A) via
  `oxygen_write_spi` (control byte = length/clock/CE + 3-byte data);
  optional AC97 via indexed read/write; EEPROM via 2-wire; GPIO +
  MPU401 UART built-in.
- Model table selects SPI codec map per subsystem (Xonar DG uses
  CS4361 + CS5361 via `xonar_dg.c`).
- Bring-up: reset DMA channels, program SPI codecs, set play routing
  (`PLAYBACK_0_TO_I2S` etc.), write DMA triples, `DMA_STATUS |= ch`,
  per-channel IRQ bits in 0x44/0x48.

**Effort: S-M** — the DMA engine is the simplest in this entire doc;
all pain is the SPI codec model table. **Value: medium-high** (Xonar
cards are still made!). **S-M / medium-high.**

### SiS SI7012 (AC97-alike)

ALSA `sound/pci/intel8x0.c`, `DEVICE_SIS`, **1039:7012**.

- Register-compatible with ICH NAM/NABM *except* position registers:
  `pos_shift` differs (CIV/PICB mean different things — ALSA uses
  `pos_shift` of 0 vs 1 and max-bdbars tweaks), interrupt-mask
  registers live at different offsets, and the cold-reset dance needs
  the SiS variant of `GLOB_CNT` handling.
- **Effort: S** once intel8x0 compat is solid. **Value: low-medium**
  (SiS boards only). **S / low.**

### NeoMagic NM256AV/ZX (laptop chip with on-chip RAM)

ALSA `sound/pci/nm256/` (`nm256.c`, `nm256_coef.c`).

| ID | chip |
|---|---|
| 10c8:8005 | NM256AV audio |
| 10c8:8006 | NM256ZX audio |
| 10c8:8016 | NM256XL+ audio |

- **BAR0 = MMIO**, two regions: control port + the **on-chip audio
  buffer RAM** (no host DMA for samples — you memcpy audio into
  card RAM at `buffer_start..buffer_end`).
- Regs: `NM_INT_REG` 0xa04 (ack: write val<<1), playback regs at 0x0,
  record regs at 0x200, rate at +2 (`STEREO|BITS_16|rate`), enable
  reg +1 (`ENABLE|ONESHOT|FREERUN`), buffer START/END/WMARK/CURRP
  pointers (dword regs at +4..+0x14), mute reg +0x18.
- **Coefficient table required** (`nm256_coef.c`, ~5KB+0x1260
  per-stream, in-tree data — no external file). Loaded into card RAM
  at `COEFF_START/END` offsets before first playback.
- AC97 via mixer regs at `NM_MIXER_OFFSET` 0x600 in port2 window;
  mixer-ready bit at 0xa04/0xa06; BIOS signature check `NM_SIGNATURE
  0x4e4d0000` at buffer top.
- Quirks: `vaio_hack`, two `reset_workaround` variants for freezing
  laptops, `use_cache` for coefficient table.

**Effort: M** (card-RAM model is unusual — no DMA at all!). **Value:
low** (rare laptops). **M / low.**

### Ensoniq ES1370/1371 — DONE (`audioes1370.cc`)

| ID | chip |
|---|---|
| 1274:5000 | ES1370 AudioPCI |
| 1274:1371 | ES1371/ES1373/CT5880 (ALSA `ens1370.c` covers 1371/5880 too) |
| 1274:5880 | CT5880 (SB PCI 128) |

Already implemented — the pattern (IO BAR + small reg file + linear
frame-count DMA + AC97 or built-in mixer) is our reference shape.
Its `es1370` "SCTRL/CTRL + DAC1/DAC2/ADC frame regs" model maps well
to the `audiopci` helper API sketched below.

---

## 9. C-Media 8338/8738/8768 (cmipci) + Oxygen (see §8)

ALSA `sound/pci/cmipci.c`.

### PCI IDs

| ID | chip |
|---|---|
| 13f6:0011 | CMI8738 (early) |
| 13f6:0100 | CMI8338A |
| 13f6:0101 | CMI8338B |
| 13f6:0111 | CMI8738 / CMI8768 |
| 13f6:0112 | CMI8738B |
| 13f6:0211 | CMI8738 (later rev) |
| 10b9:0111 | ALi-labeled CM8738 (also matches) |
| 13f6:8788 | Oxygen → §8, different driver |

### Interface

- **BAR0 = IO**, ~0x100. Chip model is readable from `CM_REG_CHFORMAT`
  bits 28-24 (`CM_CHIP_MASK1`, 037) and `CM_REG_INT_HLDCLR` bits 24-31
  (`CM_CHIP_MASK2`: 039, 039_6CH, 055, 8768).
- DMA: **two independent DMA channels** ch0/ch1 (each can be playback
  or capture, `CM_CHADC0/1`):
  - `CM_REG_CH0_FRAME1` 0x80 = buffer phys base (write) /
    `CM_REG_CH0_FRAME2` 0x84 = current position (read);
    ch1 at 0x88/0x8c. Frame2 packs `bufsize` (lo16, samples) and
    `fragsize` (hi16) — interrupts fire per fragment.
  - `CM_REG_FUNCTRL0` 0x00: `CHEN0/1` run, `PAUSE`, `CHADC` direction,
    `RST_CH` resets.
  - `CM_REG_FUNCTRL1` 0x04: `DSFC`/`ASFC` sample-rate divisors,
    `BREQ` bus-master enable, `INTRM`, SPDIF bits, legacy enables.
  - `CM_REG_CHFORMAT` 0x08: `CH0FMT/CH1FMT` (16-bit/stereo), 4/6ch
    (`CHB3D`/`CHB3D5C`), 88/96/128/176k rate bits.
  - `CM_REG_INT_HLDCLR` 0x0c irq hold/clear + chip-id bits;
    `INT_STATUS` 0x10 (CHINT0/1, busy, MCB); `LEGACY_CTRL` 0x14;
    `MISC_CTRL` 0x18 (RESET, PWD, ENDBDAC, XCHGDAC, SPDIF48K).
- Mixer: **SB16-style mixer port** (index 0x23/data 0x22 shadow regs
  `CM_REG_MIXER0` 0x20, `MIXER1` 0x24, `MIXER2` 0x25, `AUX_VOL` 0x26,
  `MISC` 0x27) — native hardware volume regs, no AC97 needed for
  basic analog volume; optional extern codec via `CM_REG_AC97` 0x28.
- Also: SB16-compatible PCM via `CM_REG_SB16_ADDR/DATA`, MPU401 PCI
  window at 0x40-0x4f, FM window at 0x50-0x5f.
- Bring-up: `MISC_CTRL |= RESET`, clear, program frame regs + format +
  freq, `FUNCTRL0 |= CHEN0`, IRQs via `INT_HLDCLR |= CH*_INT_EN`.
- Quirks: `USE_VAR48KRATE` is broken upstream; `SETLAT48`, `EDGEIRQ`;
  model-037/039/055/8768 bit differences documented in the reg defines.

**Effort: S** — arguably the simplest two-channel DMA chip in the list;
mixer is memory-bar free. **Value: high** — CMI8738 is *the* cheap PCI
sound card still sold today. **S / high.**

---

## 10. Exotic / pro / DSP cards

### ALi M5451 (`sound/pci/ali5451/`)

| 10b9:5451 | ALi M5451 South Audio |

- **IO BAR**; register map is the *same family* as Trident 4DWave:
  `ALI_START` 0x80/`ALI_STOP` 0x84/`ALI_CSPF` 0x90/`ALI_AINT` 0x98/
  `AINTEN` 0xa4/`ALI_GC_CIR` 0xa0/`VOLUME` 0xa8/`STIMER` 0xc8/
  `GLOBAL_CONTROL` 0xd4; per-voice regs `ALI_CSO_ALPHA_FMS` 0xe0,
  `ALI_LBA` 0xe4, `ALI_ESO_DELTA` 0xe8, `ALI_GVSEL_PAN_VOC_CTRL_EC`
  0xf0, `ALI_EBUF1` 0xf4, `EBUF2` 0xf8 — i.e. Trident's channel regs
  verbatim (shared 4DWave heritage).
- 32 PCM voices + reserved channels for SPDIF/center/LFE/surr/modem.
- AC97 via `ALI_AC97_WRITE` 0x40 / `ALI_AC97_READ` 0x44; codec reset
  reaches through companion chips M1533/M7101 (PCI config pokes —
  `pci_m1533`/`pci_m7101` device lookup quirk).
- **Effort: S-M** — nearly a clone of the trident engine; could share
  a "4dwave" voice layer. **Value: low-medium.** **S-M / low.**

### RME Digi9652 / Digi9636 Hammerfall (`sound/pci/rme9652/`)

| 10ee:3fc4 | RME Hammerfall / Hammerfall Light (Xilinx FPGA PCI) |

- **IO-space mapped** register window (`RME9652_IO_EXTENT` 1KB):
  `STATUS` 0x00 (lock/sync bits, buffer-pos bits 6-15 + rev1.5
  hi bits 29-31, `RME9652_F_*` freq field, `DS` double-speed, ERF
  spdif-error, tc timecode, buffer_id toggle), `PLAY_BUFFER` 0x20 &
  `REC_BUFFER` 0x24 (phys ptrs to channel buffers),
  `CONTROL` 0x40 (start/master/IE/freq/DS/PRO/EMP/Dolby/opt_out/wsel/
  inp/SyncPref/SPDIF_RESET/SELECT/CLOCK/WRITE/ADAT1_INT),
  `IRQ_CLEAR` 0x60, `TIME_CODE` 0x64, `THRU` 0x80-0xe4 per-channel
  passthru enables.
- DMA: **26 channel buffers × 64KB** (9636: 18ch) each 16K samples —
  allocate 27×64KB contiguous DMA area (driver notes +1 channel
  because hw writes 1 byte past end!). Buffer layout: N mono streams.
- All digital IO: 3×ADAT + S/PDIF each direction. No analog, no AC97.
- IRQ: toggle `buffer_id` in status; `IE` in control reg; `IRQ_CLEAR`
  write. Latency = `512B * 2^n` (n = bits 3..1 of control reg).
- `precise_ptr` quirk is unreliable by upstream comment.
- **Effort: M** (fixed-function, no firmware! the FPGA is
  self-contained — the only pro card here without blobs).
  **Value: low** (pro audio, ADAT only — useless without an ADAT DAC).
  **M / low** but *technically the easiest "pro" card*.

### Korg 1212 I/O (`sound/pci/korg1212/`)

| 10b5:906d | PLX 9060SD + Analog Devices ADSP-2106x SHARC (Korg 1212 I/O) |

- MMIO regs: mailboxes 0x40-0x4c (4×dword), OUT/IN doorbells
  0x60/0x64, status/int-ctrl 0x68, PCI control 0x6c (+ sensitivity
  word 0x6e), DEV_VEND_ID 0x70.
- Command model: doorbell commands `DB_*` (RebootCard 0xa0,
  BootFromDSPPage4 0xa4, StartDSPDownload 0xaf, DSPDownloadDone 0xae,
  ConfigureBufferMemory 3, SetClockSourceRate 5, SelectPlayMode 2,
  TriggerPlay 1...); card ACKs by writing command|0x8000 into
  mailbox; state machine `UNINITIALIZED → DSP_IN_PROCESS →
  DSP_COMPLETE → READY → OPEN → SETUP → PLAYING`.
- Buffers: 8 periods × 1024 frames × 12ch `KorgAudioFrame` (10 16-bit
  + 2 32-bit channels, analog 2ch + ADAT 8ch + SPDIF).
- **Firmware required: `korg/k1212.dsp`** — no DSP, no card.
- **Effort: L. Value: very low.** Skip.

### Echo Audio (DSP563xx) — Darla/Gina/Layla/Mia/Mona/Indigo/Echo3G (`sound/pci/echoaudio/`)

| 1057:1801 | Motorola DSP56301 (Darla20, Gina20, Layla20, Darla24, Gina24, Layla24, Mona) — subvendor 0xECC0, subdevice picks model |
| 1057:3410 | DSP56361 (Mia, Indigo, Echo3G, Indigo DJ/IO/IOx/DJx) |

- Shared `echoaudio.c` core + per-card `*.c` defining `ECHOCARD_*`
  capability macros and `card_fw[]` names.
- DSP firmware per model: `ea/darla24_dsp.fw`, `ea/gina24_dsp.fw`,
  `ea/layla24_dsp.fw`, `ea/mia_dsp.fw`, `ea/mona_*_dsp.fw` (5 files),
  `ea/indigo*_dsp.fw`, `ea/echo3g_dsp.fw`, `ea/loader_dsp.fw`...
  **all external blobs**.
- Command layer: DSP handshake via `get_dsp_pointer` shared-memory
  command pipe; **scatter-gather DMA lists** (1 page = 512 SG
  descriptors, period granularity 32 frames); "pipes"/"busses"
  resource model (input/output pipes routed to hardware busses).
- Formats incl. odd ones (S24_3LE, S32_BE); sample rate is global.
- **Effort: L. Value: very low (firmware + rarity).** Skip.

### Digigram LoLa (`sound/pci/lola/`)

| 1369:0001 | LoLa PCIe (broadcast/multichannel) |

- **Two MMIO BARs**: BAR0 = control + **HDA-style CORB/RIRB**
  (CORBWP/RIRBWP rings, 256 entries, codec "verbs"
  `nid<<20 | verb<<8 | data` — same shape as our `hda.cc` codec cmd
  path!). BAR2 = stream/DMA: DSD (DMA stream descriptor) registers per
  stream, `DINTSTS`/`DIINTSTS`/`DOINTSTS` + `DINTCTL` masks,
  `BOARD_MODE`, per-DSD `STS` (DESE/BCIS flags).
- Codec tree discovered via `LOLA_PAR_*` parameter reads through the
  verb ring — NID/verb model mirrors HD Audio. Sample rate min 16kHz
  configurable granularity.
- No firmware needed (ROM-resident).
- **Effort: M** — if we ever want a second CORB/RIRB target after
  hda.cc this is the closest design. **Value: very low** (rare
  broadcast card). **M / very low.**

### Digigram LX6464ES (`sound/pci/lx6464es/`)

| 10b5:9056 | PLX 9056 + Xilinx FPGA w/ MicroBlaze (LX6464ES / -CAE / e / e-CAE — subsystem 1369:* distinguishes) |

- MMIO via PLX BARs; Xilinx reset bit `CHIPSC_RESET_XILINX` in PLX
  user reg; MicroBlaze firmware **embedded in driver** (large hex
  arrays in `lx6464es` sources — check `lx_commands.c`/headers; the
  driver does not call request_firmware).
- Pipe-based DSP API: `lx_pipe_allocate/start/pause/stop/release`,
  `lx_set_granularity` (period size, `MICROBLAZE_IBL_MIN/MAX`),
  `lx_stream_set_format`; up to 64 channels, 8-192kHz.
- IRQ: command-ack + period notifications through shared-mem
  status pages.
- **Effort: L. Value: very low.** Skip.

### Digigram miXart (`sound/pci/mixart/`)

| 1057:0003 | Motorola MC8240 PowerPC (miXart 8 / miXart 8 AES/EBU) |

- Big-endian shared-memory mailbox: `MIXART_FLOWTABLE_PTR`,
  `PSEUDOREG_BOARDNUMBER`, message queue `snd_mixart_send_msg` with
  `MSG_STREAM_*`/`MSG_CLOCK_*`/`MSG_SYSTEM_*` uids, flowinfo +
  bufferinfo tables in host RAM describing ring buffers.
- **Firmware required: `mixart/miXart8.xlx` (Xilinx bitstream),
  `mixart/miXart8.elf` (PPC program — big-endian ELF32 loaded by
  `mixart_load_elf`), `mixart/miXart8AES.xlx`** for AES model.
- **Effort: L. Value: very low (firmware).** Skip.

### Digigram PCXHR (`sound/pci/pcxhr/`)

| 10b5:9656 | PLX PCI 9656 + Xilinx (VX882HR/PCX882HR/VX881HR/PCX881HR/VX1222HR/PCX1222HR/VX1221*/VX442*/VX822* — subsystem 1369:b*01) |
| 10b5:9056 | PLX 9056 (the "e"/non-HR variants + VX222HR/e/PCX22/PCX924 — subsystem 1369:b*21/ba/bb/bc/bd/bf) |

- RMH (register-mailbox-host) command protocol + PLL register
  (`pcxhr_pll_freq_register` — 28.224MHz/2 reference), host DMA rings.
- **Firmware required**: whole file sets per model —
  `pcxhr/xlxint.dat`, `xlxc{882hr,882e,1222hr,1222e,222,924}.dat`,
  `dspe882.e56`, `dspb*.b56`, `dspd*.d56`, `l_1_*.d56`... (MODULE_FIRMWARE
  lists ~20). Two Xilinx images (boot + main) + 3 DSP files.
- **Effort: L. Value: very low (firmware).** Skip.

### Digigram VX222 (`sound/pci/vx222/` + `sound/drivers/vx/vx_core.c`)

| 10b5:9050 | PLX 9050 (VX222 "old") — subsystem 1369:* |
| 10b5:9030 | PLX 9030 (VX222 v2/Mic) — subsystem 1369:* |

- Two IO BARs (BAR1+BAR2 used); Xilinx + DSP; simple
  input/output level mixer.
- **Firmware required** via `SND_VX_FW_LOADER`: `vx/bx_1_vxp.b56`
  (Xilinx boot), `vx/x1_1_vx2.xlx` (Xilinx), `vx/l_1_v22.d56` (DSP) —
  names vary per card type (v2/mic).
- **Effort: M-L. Value: very low (firmware).** Skip.

### AudioScience ASI (asihpi)

| 104c:a106 | TI TMS320C6205 DSP + AudioScience subvendor (175c:*) — HPI_6205 cards (ASI62xx/63xx/87xx) |
| 104c:ac60 | TI PCI2040 PCI-DSP bridge + C6000 DSP — HPI_6000 cards (ASI6xxx) |

- **HPI protocol**: message/response structs (`hpi_message`/
  `hpi_response`) sent via adapter shared-memory mailbox
  (`hpios_*`/control block); object/handle model — streams, mixers,
  clocks, volumes are objects referenced by handles; host buffer
  attach/detach messages grant bus-master DMA rights to adapter.
- DSP code **baked into the driver** (`hpi6205.c`/`hpi6000.c` contain
  compiled DSP images as C arrays — no external blob needed!).
- **Effort: L** (big protocol, adapter abstraction). **Value: low.**
  Skip, but note it as the reference for "mailbox protocol" drivers.

### Aureal Vortex AU8810/20/30 (`sound/pci/au88x0/`)

| 12eb:0001 | AU8810 Vortex (A3D) |
| 12eb:0002 | AU8820 Vortex-2 |
| 12eb:0003 | AU8830 Vortex Advantage (rev must be 0xfe or 0xfa!) |

- **MMIO BAR0**; "Aural DataBase" (ADB) DMA channel model — 16+ ADB
  routes each with src→mix-engine→dest; per-channel buffer
  addr/size/regs; A3D positional channels (`NR_A3D`), wavetable
  (`NR_WT`), SPDIF path.
- OpenVortex-derived register names: `vortex_core_init` programs
  codec init via AC97-ish port + resets the "GDB" sequencer.
- Quirks: VIA KT133/Apollo/AMD-Irongate host bridges need
  `vortex_fix_latency` (write 0xff to PCI cfg 0x40) +
  `vortex_fix_agp_bridge` (set bit 0x10 in host-bridge cfg 0x42) —
  real-world hardware needs the pcifix path.
- **Effort: L** (ADB route programming is complex; basic stereo
  subset is more like M). **Value: low-medium** (retro-gaming
  cards). **M-L / low.**

### Emagic Audiowerk2 (`sound/pci/aw2/`)

| 1131:7146 | Philips SAA7146A multimedia bridge (Audiowerk2, Terratec/EWS88 family, also the famous WinTV/DVB bridge chip) |

- **MMIO BAR0** (SAA7146 full multimedia register space); audio runs
  through the SAA7146's **RPS1 command-processor DMA** — you write
  little RPS1 task programs (the "RPS" virtual machine instructions:
  transfers, pause-wait-interrupt) into card memory/ring; fixed
  44.1kHz S16_LE only, up to 4ch playback + 2ch capture.
- IRQ: RPS1 assertion bits in SAA7146 INT status.
- **Effort: M** (RPS1 task model is quirky but the driver is <1000
  lines). **Value: very low.** Skip unless bored.

### Digidesign Digi 002/003 (digi00x) — **NOT PCI**

`sound/firewire/digi00x/` — IEEE-1394 (FireWire) devices, IEEE1394
vendor 0x00a07e, models `console` (0x000001) and `rack` (0x000002).
Custom isochronous streaming + async transaction protocol, NOT AV/C
fully. **Out of scope for a PCI driver set** — would need a whole
1394 OHCI stack. **Value: zero here.** Note only.

---

## 11. Firmware-blob summary

| Card | Firmware needed? |
|---|---|
| AC97 (intel8x0), VIA82xx, cmipci, ice1712, oxygen, ali5451, trident, nm256*, rme9652, es1370, aw2, au88x0, ca0106, ctxfi, lola | **No** (*nm256 coefficient table is in-tree C data, counts as embedded) |
| emu10k1 (E-mu 1010/1212/1820 only) | `emu/hana.fw`, `emu1010b.fw`, `audio_dock.fw`, `micro_dock.fw`, `emu0404.fw`, `emu1010_notebook.fw` |
| cs46xx (new-DSP build only) | `cs46xx/ba1` (~120KB) — use old-DSP in-tree image instead |
| ymfpci | `yamaha/ds1_dsp.fw`, `yamaha/ds1e_ctrl.fw` — **hard blocker** |
| korg1212 | `korg/k1212.dsp` |
| echoaudio | `ea/<model>_dsp.fw` (+loader) per model |
| mixart | `mixart/miXart8.xlx`, `miXart8.elf`, `miXart8AES.xlx` |
| pcxhr | ~20 `pcxhr/*.{dat,e56,b56,d56}` files |
| vx222 | `vx/*.{b56,xlx,d56}` per variant |
| asihpi | none external — DSP images are C arrays inside the driver |

Rule of thumb: **PLX-bridge (10b5) and Motorola-DSP (1057) cards all
need blobs. Anything with its own PCI vendor id (Creative, C-Media,
VIA, Cirrus, Trident, Yamaha*, ICE) is fine** except Yamaha DSP and
cs46xx-new-dsp. (*Yamaha is the exception.)

---

## 12. Recommended implementation order

Rationale: maximize coverage-per-effort for a bare-metal kernel with a
pull-mixer. All rankings assume stereo-16 playback only.

| # | Driver | Why |
|---|---|---|
| 0 | AC97 hardening (in-place) | Already works; add PCM_IN, VRA bit, surround volume regs, secondary codec, IRQ path. **S** |
| 1 | **cmipci** (CMI8738) | Smallest new driver (~200 LOC): 2 DMA channels, frame regs, mixer port. Card is still sold new. **S** |
| 2 | **via82xx** (686x first, 8233 second) | Ubiquitous real hardware; SGD model is simple; reuse AC97 codec layer via 0x80 dword. **S-M** |
| 3 | **trident + ali5451** (shared "4dwave" voice engine) | Same channel-reg family; two drivers for ~1.3x the effort of one. **M** |
| 4 | **oxygen** (CMI8788/Xonar) | Trivial DMA triples; model table for SPI codecs; modern-ish cards. **S-M** |
| 5 | **ca0106** (Live24/Audigy SE/LS) | Shares PTR/DATA idiom with emu10k1; 4 DMA engines; cover the AC97 + GPIO1 models first, defer SPI DAC. **M** |
| 6 | **ice1712** | Delta cards; DS-channel index/data engine + GPIO model table. **M** |
| 7 | **emu10k1** | Biggest consumer payoff: page table, 64 voices, AC97-over-port. Start with SB Live! (0002) stereo only. **L** |
| 8 | **ctxfi** | Flagship if we want it. **L** |
| 9 | **cs46xx** | Old-DSP image is in-tree; unusual but doable. **L** |
| 10 | **au88x0** | Retro curiosity; needs VIA-bridge pcifix. **M-L** |
| 11 | **nm256 / sis7012 / ens1371-extra / lola / rme9652 / aw2** | Opportunistic, low value. |
| skip | ymfpci, korg1212, echoaudio, mixart, pcxhr, vx222, lx6464es, asihpi | Firmware blobs we can't ship; huge protocols. |

---

## 13. Shared `audiopci` helper API sketch

Mirroring the `blockdev` pattern: a small registration table + common
ops so each chip driver is ~150-300 lines.

```cpp
namespace hype::audio::pci {

// ---- per-driver vtable -----------------------------------------------
struct PciAudioOps {
    // bind: claim BARs, reset chip, return impl ctx or nullptr
    void *(*bind)(const pci::PciDevice &dev, const PciAudioDriver &drv) noexcept;
    void (*release)(void *ctx) noexcept;

    bool (*start_playback)(void *ctx, const StreamCfg &cfg) noexcept;   // arm DMA
    void (*stop_playback)(void *ctx) noexcept;
    hcpp::u32 (*position)(void *ctx) noexcept;                          // frames played
    bool (*set_volume)(void *ctx, hcpp::u8 pct) noexcept;

    // optional
    bool (*irq_ack)(void *ctx) noexcept;    // returns true if card raised irq
    bool (*start_capture)(void *ctx, const StreamCfg &cfg) noexcept;
};

struct PciAudioId { hcpp::u16 vendor, device; };       // extend to subsystem match later

struct PciAudioDriver {
    const char *name;
    AudioChip chip;
    const PciAudioId *ids;                 // null-terminated
    const PciAudioOps *ops;
};

// ---- registry --------------------------------------------------------
bool pciaudio_register_driver(const PciAudioDriver *drv) noexcept;        // static init
hcpp::size_t pciaudio_driver_count() noexcept;
const PciAudioDriver *pciaudio_match(hcpp::u16 vid, hcpp::u16 did) noexcept;

// ---- shared helpers ---------------------------------------------------
struct IoBar { hcpp::u16 base; };          // also usable as MMIO phys for ioremap
bool claim_io_bar(const pci::PciDevice &d, int bar, IoBar &out) noexcept; // +enable IO+BM bits
bool claim_mmio_bar(const pci::PciDevice &d, int bar, hcpp::u32 &phys, hcpp::u32 &size) noexcept;

// AC'97 codec accessed through a chip-specific port pair —
// implement the 4-different transports we found:
struct Ac97Link {
    bool (*read)(void *impl, hcpp::u8 reg, hcpp::u16 &val) noexcept;
    bool (*write)(void *impl, hcpp::u8 reg, hcpp::u16 val) noexcept;
    void *impl;
};
Ac97Link ac97_via_nam_nabm(hcpp::u16 nambar);         // audioac97.cc style
Ac97Link ac97_via_cmd_regs(hcpp::u16 port, hcpp::u8 woff, hcpp::u8 roff,
                           hcpp::u8 statoff);         // trident/ali5451/via82xx
Ac97Link ac97_via_ptr_data(hcpp::u16 port);           // emu10k1/ca0106 AC97DATA+ADDR
Ac97Link ac97_via_dword_reg(hcpp::u16 port, hcpp::u8 off); // via82xx 0x80 dword

// ---- DMA buffer helpers ------------------------------------------------
struct DmaBuf { hcpp::u32 phys; void *virt; hcpp::u32 bytes; };
bool dma_alloc(hcpp::u32 bytes, hcpp::u32 align, DmaBuf &out) noexcept;   // pmm-backed
void dma_free(DmaBuf &) noexcept;

// generic linear "base+size+position" playback engine
// (covers cmipci/via82xx/es1370/rme9652 per-channel regs)
struct LinearChannel {
    hcpp::u32 (*set_buffer)(void *ctx, hcpp::u32 phys, hcpp::u32 bytes) noexcept;
    void (*run)(void *ctx, bool on) noexcept;
    hcpp::u32 (*pos)(void *ctx) noexcept;
};
}
```

Per-driver file shape (example `audiocmi.cc`):

```cpp
static void *cmi_bind(const pci::PciDevice &d, const PciAudioDriver &) {
    auto *c = new CmiCtx{};
    if (!claim_io_bar(d, 0, c->io)) return nullptr;
    // CM_RESET, read chip-mask bits, init mixer vol regs
    return c;
}
static const PciAudioId cmi_ids[] = {{0x13f6,0x0011},{0x13f6,0x0100},
    {0x13f6,0x0101},{0x13f6,0x0111},{0x13f6,0x0112},{0x13f6,0x0211},
    {0x10b9,0x0111},{}};
static const PciAudioDriver cmi_driver = {
    "cmipci", AudioChip::Cmi8x38, cmi_ids, &cmi_ops };
```

Integration: extend `audiomaps.h` `audio_id_maps[]` + `AudioChip` enum
with the new chips; `audiopci.cc::classify_device` already dispatches on
(vendor,device) — the registry replaces the `AudioIdMap` table or lives
next to it. `pullmix.cc` continues to render into the DMA buffer; only
`position()` is needed for buffer wrap/underrun tracking. IRQ support can
be deferred — polled `position()` works everywhere.

---

## 14. Quick PCI-ID → driver cheat table

```
0401:  1002:0006*  1013:6001/6003/6004/6005  1023:2000/2001
       1039:7012 1039:7018  104c:a106 ac60   1057:0003 1057:1801 1057:3410
       1073:0004 000a 000c 000d 0010 0012   10b5:9030 9050 9056 906d 9656
       10b9:0111 5451 5455  10c8:8005 8006 8016
       10de:01b1 003a 0059 006a 008a 00da 00ea 026b
       10ee:3fc4  1102:0002 0004 0005 0007 0008 000b
       1106:3058 3059  1131:7146  125d:1968 1978 1988 (ESS Maestro)
       1274:1371 5000 5880  12eb:0001 0002 0003
       1319:0801 (FM801)  1369:0001 (+PLX subsyst)
       13f6:0011 0100 0101 0111 0112 0211 8788
       1412:1712 1724  8086:2415..7195 (intel8x0)
0403:  8086:2668 293E ... (HDA — done)
```

* 1002:0006 = ATI/AMD? Not covered — note only.
ESS Maestro (125d:1968 es1968, 125d:1978/1988/1998/1999 maestro3) and
FM801 (1319:0801/0802) are additional easy AC97-adjacent targets worth a
footnote if we build the generic "linear DMA + codec regs" helper well —
es1968 is essentially a 6-channel version of the ES1370 model.

---

## References

- `sound/pci/` driver sources, torvalds/linux (git.kernel.org / github).
- `include/linux/pci_ids.h` — vendor/device constants cited above.
- `include/sound/emu10k1.h` — full EMU10K1/CA0102 register file.
- `sound/pci/oxygen/oxygen_regs.h` — simplest DMA engine found.
- `sound/pci/cmipci.c` — self-documenting register list (~line 40-360).
- `sound/pci/via82xx.c` — SGD register map + ACLINK PCI-config regs.
- `sound/pci/trident/trident.h` — 4DWave channel-register map.
- `sound/pci/rme9652/rme9652.c` — status/control register bit tables.
- osdev.wiki: AC97 (NAM/NABM), Intel HDA, SB16.
- pci-ids.ucw.cz — vendor sections 1102, 1013, 1023, 1073, 13f6, 1412,
  10b5, 10b9, 10c8, 10ee, 1057, 1369, 12eb, 1131, 175c.
