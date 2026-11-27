# Display engine (P1, spec #42)

2026-01-16: six tickets D1–D6 merged (`devin/hypeai-font-ui`, kernel/display/ as of `251553b` + merge `bed8bbc`).

## Subsystems
- **Mode tables**: `displaySize` (meta), `display-qxga/wvga/huxga`, `lowbitrate`/`highbitrate`, `uhdtv2` (VIC, 4K/8K), `pal` (BT.1700)
- **Modeset**: `vgadispi` — Bochs dispi ports 0x1CE/0x1CF + bochs-display MMIO BAR2+0x500; `dispi_set_mode` readback-verified, LFB via PCI BAR0 (else `port-only`)
- **Client/session API**: `clientdisplay` (open/modes/set/present/list), `foepnscr` (session bind/present/close)
- **DP**: `displayport` (DPCD, TPS1→TPS2→MSA state machine), `dpxaux` (AUX framing) — `NotSupported` without AUX hw; `msthub` stub
- **Identity/status**: `idscreen` (output ids cap 8), `id-stat` (stat blocks), `displaydpi` (17-entry table + scale factors), `edidcta` (CTA-861 parser)
- **Multi**: `multidisplay` (topology, arrange), `multidisplay-mode` (Single/Mirror/Span), `multidisplay-swich` (present: mirror blit all / span offset clip), `scrnmap` (output→fb map)
- **Capture**: `urecordscr` ring → codec_encode QOI/PNG
- **Pipeline**: `writeframe` (fb→scanout), `scaler` (nearest/bilinear Q16.16), `scaleuhd`, `fmtcvt` (pixel format convert), `uhdtv2-vision` (BT.2020/HDR10), `displayhda` (display-audio bridge), `hgc` facade

## Verification markers
`disp modes=24 outputs=2 cta=ok dpi=ok pal=ok hdr=ok dispi=0xb0c5 modeset=ok client=ok sess ok multi=2 mirror=ok span=ok swich=ok mst=stub frames=3 record=ok tiers=2 scale=bilinear writeframe=ok`

QEMU `bochs-display`: real 1280×720 via MMIO (`dispi=0xb0c5 mmio`); boot fb=0 there (no BIOS VBE path — falls back to VGA text, known gap).
