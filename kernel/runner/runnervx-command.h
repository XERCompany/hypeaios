#pragma once

#include "hcpp/types.hpp"

// RunnerVX command record (issue #115): the unit of work posted to the
// v-executor. Fixed-size, POD, carries an opcode, a priority lane, a
// payload union and the completion status the pump writes back.
namespace hype::vx {

    enum class VxOpcode : hcpp::u8 {
        Nop = 0,
        Log = 1,            // payload.text -> logger core fan-out
        AtomicBlockOp = 2,  // payload.block: lock-op on a u32 cell
        SvcRestart = 3,     // payload.svc.name -> revsvc restart
        Callback = 4,       // payload.call.fn(ctx)
    };

    // Priority lane index; also the mixer queue index (0 = first drain
    // candidate under round robin).
    enum class VxPriority : hcpp::u8 {
        Critical = 0,
        High = 1,
        Normal = 2,
        Low = 3,
    };

    enum class VxStatus : hcpp::u8 {
        Queued = 0,
        Running = 1,
        Done = 2,
        Failed = 3,
        Dropped = 4,
    };

    enum class VxBlockSub : hcpp::u8 {
        Add = 0,
        Set = 1,
        Xor = 2,
    };

    using VxCallback = void (*)(void* ctx) noexcept;

    constexpr hcpp::size_t vx_log_text_max = 40;
    constexpr hcpp::size_t vx_svc_name_max = 24;

    union VxPayload {
        char text[vx_log_text_max];  // Log
        struct {                     // AtomicBlockOp
            volatile hcpp::u32* target;
            hcpp::u32 operand;
            VxBlockSub sub;
        } block;
        struct {                     // SvcRestart
            char name[vx_svc_name_max];
        } svc;
        struct {                     // Callback
            VxCallback fn;
            void* ctx;
        } call;
    };

    struct VxCommand {
        VxOpcode opcode;
        VxPriority priority;
        VxStatus status;
        hcpp::u8 flags;
        hcpp::u32 ticket;   // pool ticket assigned by runnervx_post
        hcpp::u32 result;   // opcode-defined result (e.g. old cell value)
        VxPayload payload;
    };

    inline void vx_copy_text(char* dst, hcpp::size_t dst_max,
                             const char* src) noexcept {
        hcpp::size_t i = 0;
        if (src != nullptr) {
            while (i + 1 < dst_max && src[i] != '\0') {
                dst[i] = src[i];
                ++i;
            }
        }
        dst[i] = '\0';
    }

    inline VxCommand vxcmd_base(VxOpcode opcode, VxPriority priority) noexcept {
        VxCommand cmd{};
        cmd.opcode = opcode;
        cmd.priority = priority;
        cmd.status = VxStatus::Queued;
        return cmd;
    }

    inline VxCommand vxcmd_nop(
        VxPriority priority = VxPriority::Normal) noexcept {
        return vxcmd_base(VxOpcode::Nop, priority);
    }

    inline VxCommand vxcmd_log(
        const char* text,
        VxPriority priority = VxPriority::Normal) noexcept {
        VxCommand cmd = vxcmd_base(VxOpcode::Log, priority);
        vx_copy_text(cmd.payload.text, vx_log_text_max, text);
        return cmd;
    }

    inline VxCommand vxcmd_atomic_add(
        volatile hcpp::u32* target, hcpp::u32 operand,
        VxPriority priority = VxPriority::Normal) noexcept {
        VxCommand cmd = vxcmd_base(VxOpcode::AtomicBlockOp, priority);
        cmd.payload.block.target = target;
        cmd.payload.block.operand = operand;
        cmd.payload.block.sub = VxBlockSub::Add;
        return cmd;
    }

    inline VxCommand vxcmd_atomic_set(
        volatile hcpp::u32* target, hcpp::u32 value,
        VxPriority priority = VxPriority::Normal) noexcept {
        VxCommand cmd = vxcmd_base(VxOpcode::AtomicBlockOp, priority);
        cmd.payload.block.target = target;
        cmd.payload.block.operand = value;
        cmd.payload.block.sub = VxBlockSub::Set;
        return cmd;
    }

    inline VxCommand vxcmd_atomic_xor(
        volatile hcpp::u32* target, hcpp::u32 mask,
        VxPriority priority = VxPriority::Normal) noexcept {
        VxCommand cmd = vxcmd_base(VxOpcode::AtomicBlockOp, priority);
        cmd.payload.block.target = target;
        cmd.payload.block.operand = mask;
        cmd.payload.block.sub = VxBlockSub::Xor;
        return cmd;
    }

    inline VxCommand vxcmd_svc_restart(
        const char* name,
        VxPriority priority = VxPriority::High) noexcept {
        VxCommand cmd = vxcmd_base(VxOpcode::SvcRestart, priority);
        vx_copy_text(cmd.payload.svc.name, vx_svc_name_max, name);
        return cmd;
    }

    inline VxCommand vxcmd_callback(
        VxCallback fn, void* ctx,
        VxPriority priority = VxPriority::Normal) noexcept {
        VxCommand cmd = vxcmd_base(VxOpcode::Callback, priority);
        cmd.payload.call.fn = fn;
        cmd.payload.call.ctx = ctx;
        return cmd;
    }

    inline const char* vx_opcode_name(VxOpcode opcode) noexcept {
        switch (opcode) {
            case VxOpcode::Nop: return "nop";
            case VxOpcode::Log: return "log";
            case VxOpcode::AtomicBlockOp: return "atomic";
            case VxOpcode::SvcRestart: return "svc";
            case VxOpcode::Callback: return "callback";
        }
        return "?";
    }

    inline const char* vx_status_name(VxStatus status) noexcept {
        switch (status) {
            case VxStatus::Queued: return "queued";
            case VxStatus::Running: return "running";
            case VxStatus::Done: return "done";
            case VxStatus::Failed: return "failed";
            case VxStatus::Dropped: return "dropped";
        }
        return "?";
    }

}  // namespace hype::vx
