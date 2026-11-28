#include "kernel/block/sdhci.h"

#include "hcpp/memory.hpp"
#include "kernel/memory/pmm.h"
#include "kernel/pci/pci.h"
#include "kernel/pci/pci_io.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"

namespace {

    // ---- SDHCI register offsets (SD Host Controller Spec 3.00) ----
    constexpr hcpp::u32 reg_sdma_addr = 0x00;      // SDMA System Address
    constexpr hcpp::u32 reg_block_size = 0x04;     // u16: size + boundary
    constexpr hcpp::u32 reg_block_count = 0x06;    // u16
    constexpr hcpp::u32 reg_argument = 0x08;       // u32
    constexpr hcpp::u32 reg_transfer_mode = 0x0C;  // u16
    constexpr hcpp::u32 reg_command = 0x0E;        // u16
    constexpr hcpp::u32 reg_response = 0x10;       // 4 x u32 (R0..R3)
    constexpr hcpp::u32 reg_buffer_data = 0x20;    // u32
    constexpr hcpp::u32 reg_present_state = 0x24;  // u32
    constexpr hcpp::u32 reg_host_control = 0x28;   // u8
    constexpr hcpp::u32 reg_power_control = 0x29;  // u8
    constexpr hcpp::u32 reg_clock_control = 0x2C;  // u16
    constexpr hcpp::u32 reg_timeout_control = 0x2E;// u8
    constexpr hcpp::u32 reg_software_reset = 0x2F; // u8
    constexpr hcpp::u32 reg_int_status = 0x30;     // u16 W1C
    constexpr hcpp::u32 reg_err_status = 0x32;     // u16 W1C
    constexpr hcpp::u32 reg_int_status_en = 0x34;  // u16
    constexpr hcpp::u32 reg_err_status_en = 0x36;  // u16
    constexpr hcpp::u32 reg_int_signal_en = 0x38;  // u16
    constexpr hcpp::u32 reg_err_signal_en = 0x3A;  // u16
    constexpr hcpp::u32 reg_capabilities = 0x40;   // u32 (v3: u64)
    constexpr hcpp::u32 reg_version = 0xFE;        // u16 host ctrl version

    // Present State bits
    constexpr hcpp::u32 present_cmd_inhibit = 1u << 0;
    constexpr hcpp::u32 present_dat_inhibit = 1u << 1;
    constexpr hcpp::u32 present_buf_wr_ready = 1u << 10;
    constexpr hcpp::u32 present_buf_rd_ready = 1u << 11;
    constexpr hcpp::u32 present_card_inserted = 1u << 16;
    constexpr hcpp::u32 present_card_stable = 1u << 17;

    // Host Control 1 bits
    constexpr hcpp::u8 hc_4bit = 1u << 1;          // data width 4-bit
    constexpr hcpp::u8 hc_dma_sdma = 0x00;         // DMA select: SDMA

    // Power Control bits
    constexpr hcpp::u8 pwr_on = 0x01;
    constexpr hcpp::u8 pwr_3v3 = 0x07 << 1;        // SD bus voltage 3.3V

    // Clock Control bits
    constexpr hcpp::u16 clk_internal_en = 0x0001;
    constexpr hcpp::u16 clk_internal_stable = 0x0002;
    constexpr hcpp::u16 clk_sd_en = 0x0004;

    // Software Reset bits
    constexpr hcpp::u8 rst_all = 0x01;
    constexpr hcpp::u8 rst_cmd = 0x02;
    constexpr hcpp::u8 rst_dat = 0x04;

    // Interrupt Status bits
    constexpr hcpp::u16 st_cmd_complete = 0x0001;
    constexpr hcpp::u16 st_xfer_complete = 0x0002;
    constexpr hcpp::u16 st_dma_interrupt = 0x0008;
    constexpr hcpp::u16 st_buf_wr_ready = 0x0010;
    constexpr hcpp::u16 st_buf_rd_ready = 0x0020;
    constexpr hcpp::u16 st_error = 0x8000;

    // Error Interrupt Status register (0x32) is read/cleared wholesale
    // — the individual error bits don't need named constants here.

    // Transfer Mode bits
    constexpr hcpp::u16 tm_dma_en = 0x0001;
    constexpr hcpp::u16 tm_blk_cnt_en = 0x0002;
    constexpr hcpp::u16 tm_auto_cmd12 = 0x0004;
    constexpr hcpp::u16 tm_read = 0x0010;
    constexpr hcpp::u16 tm_multi = 0x0020;

    // Command register fields (u16 at 0x0E)
    constexpr hcpp::u16 cmd_resp_none = 0x0000;
    constexpr hcpp::u16 cmd_resp_r136 = 0x0001;    // R2 (CID/CSD)
    constexpr hcpp::u16 cmd_resp_r48 = 0x0002;     // R1/R3/R6/R7
    constexpr hcpp::u16 cmd_resp_r48b = 0x0003;    // R1b/R5b
    constexpr hcpp::u16 cmd_crc_check = 0x0008;
    constexpr hcpp::u16 cmd_index_check = 0x0010;
    constexpr hcpp::u16 cmd_data_present = 0x0020;
    constexpr hcpp::u16 cmd_type_abort = 0x00C0;

    // Capabilities bits (lower 32 at 0x40): also carries ADMA2 (bit19),
    // high-speed (bit21) and voltage support (bits 24:26), which this
    // driver doesn't need to gate on.
    constexpr hcpp::u32 cap_sdma = 1u << 22;
    constexpr hcpp::u32 cap_base_clk_mask = 0x0000FF00;  // bits 15:8, MHz

    // SD commands (CMD0 GO_IDLE is sent as literal flags value 0)
    constexpr hcpp::u8 cmd2_all_send_cid = 2;
    constexpr hcpp::u8 cmd3_send_rca = 3;
    constexpr hcpp::u8 cmd6_switch = 6;            // ACMD6 bus width
    constexpr hcpp::u8 cmd7_select = 7;
    constexpr hcpp::u8 cmd8_send_if_cond = 8;
    constexpr hcpp::u8 cmd9_send_csd = 9;
    constexpr hcpp::u8 cmd12_stop = 12;
    constexpr hcpp::u8 cmd16_set_blklen = 16;
    constexpr hcpp::u8 cmd17_read_single = 17;
    constexpr hcpp::u8 cmd18_read_multi = 18;
    constexpr hcpp::u8 cmd24_write_single = 24;
    constexpr hcpp::u8 cmd25_write_multi = 25;
    constexpr hcpp::u8 cmd41_send_op_cond = 41;    // ACMD41
    constexpr hcpp::u8 cmd55_app = 55;

    constexpr hcpp::u32 ocr_vdd_window = 0x00FF8000;  // 2.7-3.6V
    constexpr hcpp::u32 ocr_hcs = 1u << 30;           // host capacity support
    constexpr hcpp::u32 ocr_busy = 1u << 31;          // card power-up done
    constexpr hcpp::u32 ocr_ccs = 1u << 30;           // card capacity status

    // Bounded spin budgets (MMIO polls; mirrors ahci.cc conventions).
    constexpr hcpp::u32 spin_idle = 0x200000;      // inhibit drains
    constexpr hcpp::u32 spin_reset = 0x200000;     // software reset clear
    constexpr hcpp::u32 spin_cmd = 0x4000000;      // command complete
    constexpr hcpp::u32 spin_xfer = 0x10000000;    // data transfer
    constexpr hcpp::u32 spin_card_stable = 0x400000;

    constexpr hcpp::u32 acmd41_attempts = 512;     // busy poll retries
    constexpr hcpp::u64 acmd41_window_ms = 2000;   // wall-clock ceiling

    // One SDMA descriptor span: 32 KiB bounce = 64 sectors per command,
    // matching the 32K SDMA buffer boundary (block-size reg bits 14:12
    // = 0b011) so no boundary interrupt can fire mid-transfer.
    constexpr hcpp::u32 bounce_bytes = 32 * 1024;
    constexpr hcpp::u32 bounce_sectors = bounce_bytes / 512;
    constexpr hcpp::u16 sdma_boundary_32k = 3u << 12;

    volatile hcpp::u8* mmio;
    bool ready;
    bool card_sdhc;        // block-addressed (SDHC/SDXC) vs byte (SDSC)
    bool use_sdma;
    hcpp::u16 card_rca;
    hcpp::u8 spec_version; // 0 = 1.00, 1 = 2.00, 2 = 3.00
    hcpp::u32 base_clk_hz;
    hcpp::u64 sector_count;
    hcpp::u16 last_err;          // error status of the last failed cmd

    // DMA bounce buffer: BSS is identity-mapped and physically
    // contiguous, so it is DMA-safe. 32 KiB alignment keeps a full
    // bounce inside one SDMA boundary span.
    alignas(32768) hcpp::u8 bounce_buf[bounce_bytes];
    alignas(64) hcpp::u8 single_buf[512];  // verify-read scratch

    hcpp::u32 r32(hcpp::u32 off) noexcept {
        return *reinterpret_cast<volatile hcpp::u32*>(mmio + off);
    }
    hcpp::u16 r16(hcpp::u32 off) noexcept {
        return *reinterpret_cast<volatile hcpp::u16*>(mmio + off);
    }
    hcpp::u8 r8(hcpp::u32 off) noexcept {
        return *reinterpret_cast<volatile hcpp::u8*>(mmio + off);
    }
    void w32(hcpp::u32 off, hcpp::u32 v) noexcept {
        *reinterpret_cast<volatile hcpp::u32*>(mmio + off) = v;
    }
    void w16(hcpp::u32 off, hcpp::u16 v) noexcept {
        *reinterpret_cast<volatile hcpp::u16*>(mmio + off) = v;
    }
    void w8(hcpp::u32 off, hcpp::u8 v) noexcept {
        *reinterpret_cast<volatile hcpp::u8*>(mmio + off) = v;
    }

    void pause() noexcept { asm volatile("pause"); }

    // ~ms delay. Primary clock is uptime_ms (time.timer ran inside
    // stage_boot_devices); the spin cap keeps the loop bounded even if
    // the time source were dead.
    void delay_ms(hcpp::u32 ms) noexcept {
        const hcpp::u64 start = hype::time::uptime_ms();
        for (hcpp::u64 i = 0; i < static_cast<hcpp::u64>(ms) * 0x80000;
             ++i) {
            if (hype::time::uptime_ms() - start >= ms) return;
            pause();
        }
    }

    bool wait_inhibit(hcpp::u32 mask, hcpp::u32 limit) noexcept {
        for (hcpp::u32 i = 0; i < limit; ++i) {
            if ((r32(reg_present_state) & mask) == 0) return true;
            pause();
        }
        return false;
    }

    void status_clear() noexcept {
        w16(reg_int_status, 0xFFFF);
        w16(reg_err_status, 0xFFFF);
    }

    // Partial software reset: clears a wedged command/dat line after a
    // failed command so the slot stays usable. Self-clearing bits.
    void lines_reset() noexcept {
        w8(reg_software_reset, rst_cmd | rst_dat);
        for (hcpp::u32 i = 0; i < spin_reset; ++i) {
            if ((r8(reg_software_reset) & (rst_cmd | rst_dat)) == 0) {
                return;
            }
            pause();
        }
        status_clear();
    }

    // Programs the SDCLK divisor for `target_hz` (never exceeding it)
    // and starts internal + SD clock. Spec 1/2 use power-of-2 divisors
    // in bits 15:8 (field = divisor/2); spec 3 uses a 10-bit even
    // divisor in bits 15:6 (field = divisor/2, top 2 bits at 7:6).
    bool set_clock(hcpp::u32 target_hz) noexcept {
        w16(reg_clock_control, 0);   // stop everything first
        if (base_clk_hz == 0) return false;
        hcpp::u32 divisor = 1;
        hcpp::u16 field;
        if (spec_version >= 2) {     // Spec 3.00
            if (base_clk_hz > target_hz) {
                divisor = 2;
                while (divisor < 2046 && base_clk_hz / divisor > target_hz) {
                    divisor += 2;
                }
            }
            const hcpp::u32 f = divisor >> 1;
            field = static_cast<hcpp::u16>(((f & 0xFF) << 8) |
                                           ((f & 0x300) >> 2));
        } else {                     // Spec 1.00/2.00: powers of two
            while (divisor < 256 && base_clk_hz / divisor > target_hz) {
                divisor <<= 1;
            }
            field = static_cast<hcpp::u16>((divisor >> 1) << 8);
        }
        w16(reg_clock_control,
            static_cast<hcpp::u16>(field | clk_internal_en));
        for (hcpp::u32 i = 0; i < spin_reset; ++i) {
            if ((r16(reg_clock_control) & clk_internal_stable) != 0) {
                w16(reg_clock_control,
                    static_cast<hcpp::u16>(r16(reg_clock_control) |
                                           clk_sd_en));
                return true;
            }
            pause();
        }
        return false;
    }

    struct SdResponse {
        hcpp::u32 r[4];
    };

    // Issues one SD command. `cmd_flags` is the full 0x0E register
    // payload (index<<8 | type | data | index/crc check | resp type).
    // For data commands `tmode`/`blocks`/`dma_addr` program the
    // transfer; dma_addr == 0 selects programmed I/O.
    bool send_cmd(hcpp::u16 cmd_flags, hcpp::u32 arg, hcpp::u16 tmode,
                  hcpp::u16 blocks, hcpp::uintptr_t dma_addr,
                  SdResponse* resp) noexcept {
        const bool has_data = (cmd_flags & cmd_data_present) != 0;
        const hcpp::u32 inhibit = has_data
            ? (present_cmd_inhibit | present_dat_inhibit)
            : present_cmd_inhibit;
        if (!wait_inhibit(inhibit, spin_idle)) {
            lines_reset();
            return false;
        }
        status_clear();
        w32(reg_argument, arg);
        if (has_data) {
            if (dma_addr != 0) {
                w32(reg_sdma_addr,
                    static_cast<hcpp::u32>(dma_addr & 0xFFFFFFFFull));
            }
            w16(reg_block_size,
                static_cast<hcpp::u16>(sdma_boundary_32k | 512));
            w16(reg_block_count, blocks);
        }
        w16(reg_transfer_mode, tmode);
        w16(reg_command, cmd_flags);

        bool ok = false;
        for (hcpp::u32 i = 0; i < spin_cmd; ++i) {
            const hcpp::u16 st = r16(reg_int_status);
            if ((st & st_error) != 0) break;
            if ((st & st_cmd_complete) != 0) {
                w16(reg_int_status, st_cmd_complete);
                ok = true;
                break;
            }
            pause();
        }
        if (!ok || (r16(reg_err_status) != 0)) {
            last_err = r16(reg_err_status);
            status_clear();
            lines_reset();
            return false;
        }
        if (resp != nullptr) {
            for (hcpp::u8 i = 0; i < 4; ++i) {
                resp->r[i] = r32(reg_response + 4 * i);
            }
        }
        return true;
    }

    bool send_app_cmd(hcpp::u16 cmd_flags, hcpp::u32 arg,
                      SdResponse* resp) noexcept {
        SdResponse r1;
        if (!send_cmd(static_cast<hcpp::u16>((cmd55_app << 8) |
                                            cmd_crc_check |
                                            cmd_index_check |
                                            cmd_resp_r48),
                      static_cast<hcpp::u32>(card_rca) << 16, 0, 0, 0,
                      &r1)) {
            return false;
        }
        return send_cmd(cmd_flags, arg, 0, 0, 0, resp);
    }

    // SDMA transfer: caller guarantees blocks * 512 fits one 32K
    // boundary span and the bounce buffer is boundary-aligned.
    bool sdma_transfer(hcpp::u8 cmd_idx, hcpp::u32 arg, hcpp::u16 blocks,
                       bool is_read) noexcept {
        const bool multi = blocks > 1;
        const hcpp::u16 tmode = static_cast<hcpp::u16>(
            tm_dma_en |
            (is_read ? tm_read : 0) |
            (multi ? (tm_blk_cnt_en | tm_auto_cmd12 | tm_multi) : 0));
        const hcpp::u16 flags = static_cast<hcpp::u16>(
            (cmd_idx << 8) | cmd_data_present | cmd_crc_check |
            cmd_index_check | cmd_resp_r48);
        if (!send_cmd(flags, arg, tmode, blocks,
                      reinterpret_cast<hcpp::uintptr_t>(bounce_buf),
                      nullptr)) {
            return false;
        }
        for (hcpp::u32 i = 0; i < spin_xfer; ++i) {
            const hcpp::u16 st = r16(reg_int_status);
            if ((st & st_error) != 0) {
                status_clear();
                // Abort the data line; card may need CMD12.
                send_cmd(static_cast<hcpp::u16>((cmd12_stop << 8) |
                                               cmd_type_abort |
                                               cmd_resp_none),
                         0, 0, 0, 0, nullptr);
                lines_reset();
                return false;
            }
            if ((st & st_dma_interrupt) != 0) {
                // Shouldn't trigger while transfers stay inside one
                // boundary span; keep servicing it defensively so a
                // stricter host cannot wedge us here.
                w16(reg_int_status, st_dma_interrupt);
                continue;
            }
            if ((st & st_xfer_complete) != 0) {
                w16(reg_int_status, st_xfer_complete);
                return true;
            }
            pause();
        }
        status_clear();
        lines_reset();
        return false;
    }

    // Programmed-I/O fallback through the Buffer Data Port, used when
    // the controller lacks SDMA (caps bit22 clear).
    bool pio_transfer(hcpp::u8 cmd_idx, hcpp::u32 arg, hcpp::u16 blocks,
                      bool is_read, hcpp::u8* buf) noexcept {
        const bool multi = blocks > 1;
        const hcpp::u16 tmode = static_cast<hcpp::u16>(
            (is_read ? tm_read : 0) |
            (multi ? (tm_blk_cnt_en | tm_auto_cmd12 | tm_multi) : 0));
        const hcpp::u16 flags = static_cast<hcpp::u16>(
            (cmd_idx << 8) | cmd_data_present | cmd_crc_check |
            cmd_index_check | cmd_resp_r48);
        if (!send_cmd(flags, arg, tmode, blocks, 0, nullptr)) {
            return false;
        }
        hcpp::u32 words_total =
            static_cast<hcpp::u32>(blocks) * (512 / 4);
        volatile hcpp::u32* port =
            reinterpret_cast<volatile hcpp::u32*>(
                const_cast<hcpp::u8*>(mmio) + reg_buffer_data);
        hcpp::u32 words_done = 0;
        for (hcpp::u32 i = 0; i < spin_xfer && words_done < words_total;
             ++i) {
            const hcpp::u16 st = r16(reg_int_status);
            if ((st & st_error) != 0) {
                status_clear();
                lines_reset();
                return false;
            }
            const hcpp::u32 ready_bit =
                is_read ? present_buf_rd_ready : present_buf_wr_ready;
            if ((r32(reg_present_state) & ready_bit) != 0) {
                // One buffer chunk (up to block size) is ready.
                for (hcpp::u32 w = 0;
                     w < 128 && words_done < words_total; ++w) {
                    if (is_read) {
                        const hcpp::u32 v = *port;
                        buf[4 * (words_done)] =
                            static_cast<hcpp::u8>(v & 0xFF);
                        buf[4 * words_done + 1] =
                            static_cast<hcpp::u8>((v >> 8) & 0xFF);
                        buf[4 * words_done + 2] =
                            static_cast<hcpp::u8>((v >> 16) & 0xFF);
                        buf[4 * words_done + 3] =
                            static_cast<hcpp::u8>((v >> 24) & 0xFF);
                    } else {
                        *port = static_cast<hcpp::u32>(
                            buf[4 * words_done] |
                            (buf[4 * words_done + 1] << 8) |
                            (buf[4 * words_done + 2] << 16) |
                            (buf[4 * words_done + 3] << 24));
                    }
                    ++words_done;
                }
                w16(reg_int_status,
                    is_read ? st_buf_rd_ready : st_buf_wr_ready);
            }
            pause();
        }
        if (words_done < words_total) {
            status_clear();
            lines_reset();
            return false;
        }
        for (hcpp::u32 i = 0; i < spin_xfer; ++i) {
            const hcpp::u16 st = r16(reg_int_status);
            if ((st & st_error) != 0) {
                status_clear();
                lines_reset();
                return false;
            }
            if ((st & st_xfer_complete) != 0) {
                w16(reg_int_status, st_xfer_complete);
                return true;
            }
            pause();
        }
        status_clear();
        lines_reset();
        return false;
    }

    bool data_xfer(hcpp::u8 cmd_idx, hcpp::u32 arg, hcpp::u16 blocks,
                   bool is_read, hcpp::u8* buf) noexcept {
        if (use_sdma) {
            return sdma_transfer(cmd_idx, arg, blocks, is_read);
        }
        return pio_transfer(cmd_idx, arg, blocks, is_read, buf);
    }

    hcpp::u32 lba_arg(hcpp::u64 lba) noexcept {
        // SDHC/SDXC cards take block addresses; SDSC takes byte
        // addresses.
        return card_sdhc ? static_cast<hcpp::u32>(lba)
                         : static_cast<hcpp::u32>(lba * 512);
    }

    // Parses the CSD captured by CMD9 (R2, 136-bit). The response
    // registers store response bits [135:8] in reverse order — reg
    // 0x1C carries the top word — so each CSD word needs the reversed
    // register plus an 8-bit shift (the CRC7/end bit are stripped).
    bool parse_csd(const SdResponse& resp) noexcept {
        const hcpp::u32 raw[4] = {resp.r[3], resp.r[2], resp.r[1],
                                  resp.r[0]};
        hcpp::u32 csd[4];
        for (hcpp::u8 i = 0; i < 3; ++i) {
            csd[i] = (raw[i] << 8) | (raw[i + 1] >> 24);
        }
        csd[3] = raw[3] << 8;
        const hcpp::u32 structure = csd[0] >> 30;
        if (structure == 1) {
            // CSD v2.0 (SDHC/SDXC): C_SIZE = bits 69:48, capacity =
            // (C_SIZE + 1) * 512 KiB.
            const hcpp::u32 c_size =
                ((csd[1] & 0x3F) << 16) | (csd[2] >> 16);
            sector_count = (static_cast<hcpp::u64>(c_size) + 1) * 1024;
            return true;
        }
        if (structure == 0) {
            // CSD v1.0 (SDSC): capacity = (C_SIZE+1) * 2^(MULT+2)
            // * 2^READ_BL_LEN bytes.
            const hcpp::u32 read_bl_len = (csd[1] >> 16) & 0xF;
            const hcpp::u32 c_size =
                ((csd[1] & 0x3FF) << 2) | (csd[2] >> 30);
            const hcpp::u32 mult = (csd[2] >> 15) & 0x7;
            const hcpp::u64 bytes =
                (static_cast<hcpp::u64>(c_size) + 1)
                << (mult + 2 + read_bl_len);
            sector_count = bytes / 512;
            return true;
        }
        return false;
    }

    void fail_step(char step) noexcept {
        serial::write("sdhci dev=fail s=");
        serial::write_char(step);
        serial::write(" es=");
        serial::write_hex64(last_err);
        serial::write("\r\n");
    }

    // Full SD identification sequence. Each stage logs its own step id
    // on failure so a half-broken card/host is diagnosable from the
    // serial markers alone.
    bool card_init() noexcept {
        SdResponse resp;
        // CMD0 GO_IDLE_STATE: no response, no checks.
        if (!send_cmd(0, 0, 0, 0, 0, nullptr)) {
            fail_step('0');
            return false;
        }

        // CMD8 SEND_IF_COND: VHS 2.7-3.6V + check pattern 0xAA.
        bool sdv2 = false;
        if (send_cmd(static_cast<hcpp::u16>((cmd8_send_if_cond << 8) |
                                           cmd_crc_check |
                                           cmd_index_check |
                                           cmd_resp_r48),
                     0x000001AA, 0, 0, 0, &resp) &&
            (resp.r[0] & 0xFF) == 0xAA) {
            sdv2 = true;
        }
        // After a CMD8 timeout the DAT line can sit in an unknown
        // state on picky hosts; the failed send already ran the
        // CMD+DAT line reset.

        // ACMD41 SD_SEND_OP_COND: poll until the busy bit clears...
        // until the card reports power-up done (bit31). HCS declares
        // block-addressing support for SDv2 cards.
        const hcpp::u32 op_arg =
            ocr_vdd_window | (sdv2 ? ocr_hcs : 0);
        bool powered = false;
        const hcpp::u64 deadline =
            hype::time::uptime_ms() + acmd41_window_ms;
        for (hcpp::u32 i = 0; i < acmd41_attempts; ++i) {
            if (!send_app_cmd(static_cast<hcpp::u16>(
                                  (cmd41_send_op_cond << 8) |
                                  cmd_resp_r48),  // R3: no CRC check
                              op_arg, &resp)) {
                fail_step('1');
                return false;
            }
            if ((resp.r[0] & ocr_busy) != 0) {
                powered = true;
                break;
            }
            delay_ms(5);
            if (hype::time::uptime_ms() > deadline) break;
        }
        if (!powered) {
            fail_step('2');
            return false;
        }
        card_sdhc = sdv2 && (resp.r[0] & ocr_ccs) != 0;

        // CMD2 ALL_SEND_CID (R2, 136-bit) — contents unused, but the
        // card enters identification state through it.
        if (!send_cmd(static_cast<hcpp::u16>((cmd2_all_send_cid << 8) |
                                            cmd_resp_r136),
                      0, 0, 0, 0, &resp)) {
            fail_step('3');
            return false;
        }
        // CMD3 SEND_RELATIVE_ADDR (R6): RCA in bits 31:16.
        if (!send_cmd(static_cast<hcpp::u16>((cmd3_send_rca << 8) |
                                            cmd_crc_check |
                                            cmd_index_check |
                                            cmd_resp_r48),
                      0, 0, 0, 0, &resp)) {
            fail_step('4');
            return false;
        }
        card_rca = static_cast<hcpp::u16>(resp.r[0] >> 16);

        // CMD9 SEND_CSD for capacity.
        if (!send_cmd(static_cast<hcpp::u16>((cmd9_send_csd << 8) |
                                            cmd_resp_r136),
                      static_cast<hcpp::u32>(card_rca) << 16, 0, 0, 0,
                      &resp)) {
            fail_step('5');
            return false;
        }
        if (!parse_csd(resp)) {
            fail_step('8');
            return false;
        }

        // CMD7 SELECT (R1b).
        if (!send_cmd(static_cast<hcpp::u16>((cmd7_select << 8) |
                                            cmd_crc_check |
                                            cmd_index_check |
                                            cmd_resp_r48b),
                      static_cast<hcpp::u32>(card_rca) << 16, 0, 0, 0,
                      &resp)) {
            fail_step('6');
            return false;
        }

        // ACMD6 SET_BUS_WIDTH = 4-bit when the host slot supports it.
        // Failure is non-fatal: 1-bit mode still works.
        if (send_app_cmd(static_cast<hcpp::u16>((cmd6_switch << 8) |
                                               cmd_crc_check |
                                               cmd_index_check |
                                               cmd_resp_r48),
                         2, &resp)) {
            w8(reg_host_control,
               static_cast<hcpp::u8>(r8(reg_host_control) | hc_4bit));
        }

        // Raise SDCLK to full speed mode (<= 25 MHz; no high-speed bit
        // needed since we stay in default-speed range).
        set_clock(25000000u);

        // CMD16 SET_BLOCKLEN 512 (no-op on SDHC, required on SDSC).
        if (!send_cmd(static_cast<hcpp::u16>((cmd16_set_blklen << 8) |
                                            cmd_crc_check |
                                            cmd_index_check |
                                            cmd_resp_r48),
                      512, 0, 0, 0, nullptr)) {
            fail_step('7');
            return false;
        }
        return true;
    }

    // Read-only probe check: LBA 0 must not be all-0xFF (dead/floating
    // read). The MBR signature earns an extra marker. A second read of
    // 16 sectors exercises the multi-block CMD18 path (SDMA + auto
    // CMD12) and cross-checks its first sector against the
    // single-block read.
    bool verify_read() noexcept {
        hcpp::set(single_buf, 0, sizeof(single_buf));
        if (!hype::sdhci::sdhci_read(0, single_buf, 1)) return false;
        bool nonzero = false;
        bool not_ff = false;
        for (hcpp::u32 i = 0; i < 512; ++i) {
            if (single_buf[i] != 0) nonzero = true;
            if (single_buf[i] != 0xFF) not_ff = true;
        }
        if (!nonzero || !not_ff) return false;
        if (sector_count >= 16) {
            hcpp::set(bounce_buf, 0, sizeof(bounce_buf));
            if (!hype::sdhci::sdhci_read(0, bounce_buf, 16)) {
                return false;
            }
            if (hcpp::compare(bounce_buf, single_buf, 512) != 0) {
                return false;
            }
        }
        if (single_buf[510] == 0x55 && single_buf[511] == 0xAA) {
            serial::write_line("sdhci sig=55aa");
        }
        return true;
    }

}

namespace hype::sdhci {

    bool sdhci_probe() noexcept {
        ready = false;
        card_sdhc = false;
        card_rca = 0;
        sector_count = 0;
        use_sdma = false;
        mmio = nullptr;

        const pci::PciDevice* dev = nullptr;
        for (hcpp::size_t i = 0; i < pci::device_count(); ++i) {
            const pci::PciDevice& d = pci::devices()[i];
            if (d.class_code == 0x08 && d.subclass == 0x05) {
                dev = &d;
                break;
            }
        }
        if (dev == nullptr) {
            serial::write_line("sdhci dev=absent");
            return false;
        }

        const hcpp::u32 bar0 = pci::bar(*dev, 0);
        const hcpp::u64 bar_addr =
            pci::bar_memory_address(bar0, pci::bar(*dev, 1));
        if (!pci::bar_is_memory(bar0) || bar_addr == 0 ||
            bar_addr >= 0x100000000ull || !memory::pmm_initialized()) {
            serial::write_line("sdhci dev=absent");
            return false;
        }
        pci::pci_enable_mem(*dev);
        pci::pci_enable_busmaster(*dev);
        mmio = reinterpret_cast<volatile hcpp::u8*>(
            static_cast<hcpp::uintptr_t>(bar_addr));

        spec_version = static_cast<hcpp::u8>(r16(reg_version) & 0xFF);
        const hcpp::u32 caps = r32(reg_capabilities);
        base_clk_hz =
            static_cast<hcpp::u32>((caps & cap_base_clk_mask) >> 8) *
            1000000u;
        if (base_clk_hz == 0) {
            base_clk_hz = 50000000;   // sane fallback (QEMU uses 52)
        }
        const bool sdma_ok = (caps & cap_sdma) != 0;

        // Full software reset; RSTA is self-clearing.
        w8(reg_software_reset, rst_all);
        bool reset_ok = false;
        for (hcpp::u32 i = 0; i < spin_reset; ++i) {
            if ((r8(reg_software_reset) & rst_all) == 0) {
                reset_ok = true;
                break;
            }
            pause();
        }
        if (!reset_ok) {
            serial::write_line("sdhci dev=absent");
            return false;
        }
        serial::write_line("sdhci dev=ok");

        // Card detect: no card -> controller is fine but not usable.
        if ((r32(reg_present_state) & present_card_inserted) == 0) {
            serial::write_line("sdhci card=none");
            return false;
        }
        // Wait for the card-detect signal to stabilise.
        {
            hcpp::u32 i = 0;
            while ((r32(reg_present_state) & present_card_stable) == 0 &&
                   i < spin_card_stable) {
                ++i;
                pause();
            }
        }
        // 3.3V bus power + power on, then let the rail ramp.
        w8(reg_power_control, static_cast<hcpp::u8>(pwr_3v3 | pwr_on));
        delay_ms(10);

        // Maximum data-timeout counter, all statuses visible to the
        // polling loops, no interrupt signalling (pure polling).
        w8(reg_timeout_control, 0x0E);
        w16(reg_int_status_en, 0x01FF);
        w16(reg_err_status_en, 0xFFFF);
        w16(reg_int_signal_en, 0);
        w16(reg_err_signal_en, 0);
        status_clear();

        // Identification clock: 400 kHz.
        if (!set_clock(400000u)) {
            fail_step('c');
            return false;
        }
        delay_ms(2);   // >= 74 SDCLK cycles after power-up

        // Host control: 1-bit data, SDMA selected when supported.
        use_sdma = sdma_ok;
        w8(reg_host_control, hc_dma_sdma);

        if (!card_init()) {
            return false;
        }

        serial::write("sdhci card=");
        serial::write(card_sdhc ? "sdhc" : "sdsc");
        serial::write(" cap=");
        serial::write_dec32(
            static_cast<hcpp::u32>(sector_count / 2048));
        serial::write("MB\r\n");

        ready = true;
        const bool verified = verify_read();  // may print sig=55aa
        serial::write("sdhci rw=");
        serial::write_line(verified ? "ok" : "fail");
        return true;
    }

    bool sdhci_ready() noexcept {
        if (!ready) return false;
        if ((r32(reg_present_state) & present_card_inserted) == 0) {
            ready = false;   // card pulled
            return false;
        }
        return true;
    }

    hcpp::u64 sdhci_sectors() noexcept { return sector_count; }

    bool sdhci_read(hcpp::u64 lba, void* buffer,
                    hcpp::u32 sectors) noexcept {
        if (!sdhci_ready() || buffer == nullptr || sectors == 0 ||
            lba + sectors > sector_count) {
            return false;
        }
        hcpp::u8* out = static_cast<hcpp::u8*>(buffer);
        while (sectors != 0) {
            const hcpp::u16 chunk = static_cast<hcpp::u16>(
                sectors < bounce_sectors ? sectors : bounce_sectors);
            const hcpp::u8 cmd_idx =
                chunk > 1 ? cmd18_read_multi : cmd17_read_single;
            if (use_sdma) {
                if (!data_xfer(cmd_idx, lba_arg(lba), chunk, true,
                               nullptr)) {
                    return false;
                }
                hcpp::copy(out, bounce_buf,
                           static_cast<hcpp::size_t>(chunk) * 512);
            } else {
                if (!data_xfer(cmd_idx, lba_arg(lba), chunk, true,
                               out)) {
                    return false;
                }
            }
            lba += chunk;
            out += static_cast<hcpp::size_t>(chunk) * 512;
            sectors -= chunk;
        }
        return true;
    }

    bool sdhci_write(hcpp::u64 lba, const void* buffer,
                     hcpp::u32 sectors) noexcept {
        if (!sdhci_ready() || buffer == nullptr || sectors == 0 ||
            lba + sectors > sector_count) {
            return false;
        }
        const hcpp::u8* in = static_cast<const hcpp::u8*>(buffer);
        while (sectors != 0) {
            const hcpp::u16 chunk = static_cast<hcpp::u16>(
                sectors < bounce_sectors ? sectors : bounce_sectors);
            const hcpp::u8 cmd_idx =
                chunk > 1 ? cmd25_write_multi : cmd24_write_single;
            if (use_sdma) {
                hcpp::copy(bounce_buf, in,
                           static_cast<hcpp::size_t>(chunk) * 512);
                if (!data_xfer(cmd_idx, lba_arg(lba), chunk, false,
                               nullptr)) {
                    return false;
                }
            } else {
                if (!data_xfer(cmd_idx, lba_arg(lba), chunk, false,
                               const_cast<hcpp::u8*>(in))) {
                    return false;
                }
            }
            lba += chunk;
            in += static_cast<hcpp::size_t>(chunk) * 512;
            sectors -= chunk;
        }
        return true;
    }

    bool sdhci_flush() noexcept { return sdhci_ready(); }

}
