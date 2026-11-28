#include "kernel/runner/runnervx.h"

#include "hcpp/atomic.h"
#include "hcpp/string_view.hpp"
#include "kernel/log/logger.h"
#include "kernel/svc/revsvc.h"

namespace hype::vx {

    namespace {

        struct VxSlot {
            VxCommand cmd;
            volatile hcpp::u32 used;
        };

        VxSlot pool[vx_pool_capacity];
        VxMixer mixer;
        volatile hcpp::u32 next_ticket = 0;
        VxStats stats{};

        hcpp::i32 alloc_slot() noexcept {
            for (hcpp::size_t i = 0; i < vx_pool_capacity; ++i) {
                if (hcpp::atomic::compare_exchange32(&pool[i].used, 0, 1)) {
                    return static_cast<hcpp::i32>(i);
                }
            }
            return -1;
        }

        void free_slot(hcpp::u32 index) noexcept {
            hcpp::atomic::store32(&pool[index].used, 0);
        }

        bool execute(VxCommand& cmd) noexcept {
            switch (cmd.opcode) {
                case VxOpcode::Nop:
                    return true;
                case VxOpcode::Log:
                    hype::log::log_write(
                        hcpp::StringView(cmd.payload.text));
                    return true;
                case VxOpcode::AtomicBlockOp: {
                    volatile hcpp::u32* target = cmd.payload.block.target;
                    if (target == nullptr) {
                        return false;
                    }
                    switch (cmd.payload.block.sub) {
                        case VxBlockSub::Add:
                            cmd.result = hcpp::atomic::add32(
                                target, cmd.payload.block.operand);
                            return true;
                        case VxBlockSub::Set:
                            cmd.result = hcpp::atomic::exchange32(
                                target, cmd.payload.block.operand);
                            return true;
                        case VxBlockSub::Xor: {
                            for (;;) {
                                const hcpp::u32 old =
                                    hcpp::atomic::load32(target);
                                if (hcpp::atomic::compare_exchange32(
                                        target, old,
                                        old ^ cmd.payload.block.operand)) {
                                    cmd.result = old;
                                    return true;
                                }
                            }
                        }
                    }
                    return false;
                }
                case VxOpcode::SvcRestart:
                    return hype::svc::svc_restart(cmd.payload.svc.name);
                case VxOpcode::Callback:
                    if (cmd.payload.call.fn == nullptr) {
                        return false;
                    }
                    cmd.payload.call.fn(cmd.payload.call.ctx);
                    return true;
            }
            return false;
        }

        void selftest_flag(void* ctx) noexcept {
            *static_cast<volatile bool*>(ctx) = true;
        }

    }  // namespace

    bool runnervx_init() noexcept {
        for (hcpp::size_t i = 0; i < vx_pool_capacity; ++i) {
            pool[i].used = 0;
            pool[i].cmd = VxCommand{};
        }
        vxmixer_init(mixer);
        next_ticket = 0;
        stats = VxStats{};
        return true;
    }

    hcpp::u32 runnervx_post(const VxCommand& cmd) noexcept {
        const hcpp::i32 index = alloc_slot();
        if (index < 0) {
            ++stats.dropped;
            return vx_invalid_ticket;
        }
        const hcpp::u32 ticket = hcpp::atomic::add32(&next_ticket, 1);
        pool[index].cmd = cmd;
        pool[index].cmd.ticket = ticket;
        pool[index].cmd.status = VxStatus::Queued;
        if (!vxmixer_push(mixer, static_cast<hcpp::u32>(cmd.priority),
                          static_cast<hcpp::u32>(index))) {
            pool[index].cmd.status = VxStatus::Dropped;
            free_slot(static_cast<hcpp::u32>(index));
            ++stats.dropped;
            return vx_invalid_ticket;
        }
        ++stats.posted;
        ++stats.pending;
        return ticket;
    }

    hcpp::u32 runnervx_pump() noexcept {
        hcpp::u32 batch[vx_pump_max];
        const hcpp::u32 count =
            vxmixer_drain(mixer, batch, vx_pump_max);
        hcpp::u32 executed = 0;
        for (hcpp::u32 i = 0; i < count; ++i) {
            const hcpp::u32 index = batch[i];
            if (index >= vx_pool_capacity || pool[index].used == 0) {
                continue;
            }
            VxCommand& cmd = pool[index].cmd;
            cmd.status = VxStatus::Running;
            const bool ok = execute(cmd);
            cmd.status = ok ? VxStatus::Done : VxStatus::Failed;
            if (ok) {
                ++stats.executed;
            } else {
                ++stats.failed;
            }
            if (stats.pending != 0) {
                --stats.pending;
            }
            ++executed;
            // Ticket lookups stay valid until the slot is reposted.
            free_slot(index);
        }
        return executed;
    }

    VxStatus runnervx_status(hcpp::u32 ticket) noexcept {
        if (ticket == vx_invalid_ticket) {
            return VxStatus::Dropped;
        }
        for (hcpp::size_t i = 0; i < vx_pool_capacity; ++i) {
            if (pool[i].cmd.ticket == ticket) {
                return pool[i].cmd.status;
            }
        }
        return VxStatus::Dropped;
    }

    VxStats runnervx_stats() noexcept {
        VxStats snapshot = stats;
        snapshot.pending = vxmixer_pending(mixer);
        return snapshot;
    }

    bool runnervx_selftest() noexcept {
        volatile hcpp::u32 counter = 0;
        volatile bool flag = false;
        const hcpp::u32 tickets[3] = {
            runnervx_post(vxcmd_nop(VxPriority::High)),
            runnervx_post(vxcmd_atomic_add(&counter, 7)),
            runnervx_post(
                vxcmd_callback(&selftest_flag, const_cast<bool*>(&flag))),
        };
        if (tickets[0] == vx_invalid_ticket ||
            tickets[1] == vx_invalid_ticket ||
            tickets[2] == vx_invalid_ticket) {
            return false;
        }
        const hcpp::u32 executed = runnervx_pump();
        return executed == 3 && counter == 7 && flag &&
               runnervx_status(tickets[0]) == VxStatus::Done &&
               runnervx_status(tickets[1]) == VxStatus::Done &&
               runnervx_status(tickets[2]) == VxStatus::Done;
    }

}  // namespace hype::vx
