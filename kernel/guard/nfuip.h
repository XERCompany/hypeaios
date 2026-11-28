#pragma once

#include "hcpp/types.hpp"
#include "kernel/guard/guard.h"
#include "kernel/guard/rulez.h"

namespace hype::guard {

    // Nfuip — NetFilter-UI-Policy: the guard's net-op hook layer, the
    // packet-filter counterpart to landlock's fs sandbox. Net ops are
    // expressed as op bits (nfuip_op_*) and object strings
    // ("net:udp:bind:<port>", "net:udp:send:<dst_port>"), which lets the
    // same Rulez prefix-matching engine drive net policy — a rule on
    // prefix "net:udp:send" covers every send, one on
    // "net:udp:bind:53" pins DNS-port binds.
    //
    // kernel/net/net.cc consults nfuip_check() at the two socket choke
    // points (net_udp_bind / net_udp_send); the C++ side marshals into
    // nfuip_gate (nfuip.S), a tiny asm entry/ret skeleton that forwards
    // to the extern-"C" checker nfuip_check_c — keeping one compiled
    // gate callable from both C and future asm call sites.

    enum NfuipOp : hcpp::u32 {
        nfuip_op_udp_bind = 1u << 0,
        nfuip_op_udp_send = 1u << 1,
        nfuip_op_all      = 0x3u,
    };

    // The net policy domain (single-domain stack — net ops have no
    // per-app layering yet; the landlock model can be layered on top
    // later without changing the wire format).
    Rulez& nfuip_rules() noexcept;
    // Adds a rule to the net domain; returns the rule id (0 = fail).
    hcpp::u16 nfuip_add_rule(const char* object_prefix, hcpp::u32 ops_mask,
                             RuleAction action) noexcept;
    // Net-op choke point: formats the object string for `op`+`port`
    // and runs it through guard_check(GuardOp::Net, current subject).
    GuardResult nfuip_check(hcpp::u32 op_bit, hcpp::u16 port) noexcept;
    // The guard hook nfuip installs at guard_init: evaluates the net
    // domain for GuardOp::Net checks, allow otherwise.
    GuardResult nfuip_hook(GuardOp op, const Subject& subject,
                           const char* object, hcpp::u32 aux) noexcept;

}  // namespace hype::guard

// C ABI used by nfuip.S and external gates. nfuip_check_c does the
// real work (runs the current subject through guard_check for
// GuardOp::Net); nfuip_gate is the asm-side entry/ret skeleton.
extern "C" hcpp::u32 nfuip_check_c(hcpp::u64 op_bit,
                                 hcpp::u64 port) noexcept;
extern "C" hcpp::u32 nfuip_gate(hcpp::u64 op_bit,
                              hcpp::u64 port) noexcept;  // nfuip.S
