#pragma once

#include "hcpp/types.hpp"

// Software watchdog for HypeAI OS (issue #140): heartbeat sources plus
// a monitor that trips when a watched subsystem stops making forward
// progress — the "hung kernel/UI" detector.
//
// Model
// -----
// A subsystem registers a named source via wdt_register() and calls
// wdt_pet()/wdt_pet_name() from its real periodic path (context-switch
// point, UI idle pump, pointer poll...). Sources start DISARMED: the
// first pet arms the deadline, so a source whose subsystem never comes
// up (the shell before login, a test-only entry) can never false-trip.
//
// Two checkers watch the same table:
//   1. The "wdt" monitor thread — a sched kernel thread with a ~500 ms
//      period — scans every armed source and reacts to a stale
//      heartbeat:
//        * real source                -> serial "wdt trip=<name>" then
//          panic(): the panic path writes the crash dump
//          (U:/crash.log or the scratch LBA) and halts, so the trip is
//          observable on serial instead of hidden by a reboot loop;
//        * log-only source            -> the same "wdt trip=" marker
//          is printed once and latched in `tripped`; the machine keeps
//          running. This is TEST MODE (flag_log_only): the "wdt-test"
//          source registered by wdt_initialize() exercises the whole
//          trip path end-to-end without killing the kernel.
//   2. wdt_irq_check(), called from the vector-32 timer handler on
//      every CPU BEFORE sched::tick(): if the monitor thread itself has
//      been starved past monitor_deadline_ms the scheduler can no
//      longer run the watchdog — no thread context remains that could
//      notice — so the IRQ panics directly. This is the detection path
//      for a full scheduler/UI freeze: the heartbeat comparison lives
//      inside the IRQ, not inside the thing being watched.
//
// All shared state is volatile + __atomic: the table is written from
// IRQ context (pets on the ctx-switch path run under rq_lock inside
// the timer tick) and from thread context with no extra locking, so
// wdt_pet() stays callable everywhere, including under rq_lock.

namespace hype::wdt {

    constexpr hcpp::u64 default_timeout_ms = 5000;    // per-source trip window
    constexpr hcpp::u64 check_interval_ms = 500;      // monitor thread period
    constexpr hcpp::u64 monitor_deadline_ms = 10000;  // IRQ-side starvation limit
    constexpr hcpp::u64 selftest_timeout_ms = 1200;   // wdt-test trip window
    constexpr hcpp::size_t max_sources = 8;
    constexpr hcpp::u8 invalid_id = 0xFF;

    // wdt_register() flags. Without flag_log_only a trip is REAL: serial
    // marker + panic + crash dump + halt. With it, the trip is TEST
    // MODE: the "wdt trip=<name>" marker is emitted once and latched
    // (see wdt_tripped) and the kernel keeps running — no panic.
    constexpr hcpp::u8 flag_log_only = 0x01;

    // Registers a heartbeat source (name truncated to 11 chars).
    // timeout_ms == 0 selects default_timeout_ms. Returns the source id
    // or invalid_id when the table is full/name is empty. Registration
    // is init-time only; it is not re-entrant against itself.
    hcpp::u8 wdt_register(const char* name, hcpp::u8 flags,
                          hcpp::u64 timeout_ms) noexcept;

    // Heartbeat. The FIRST pet on a source arms its deadline; later
    // pets refresh it. IRQ-safe and rq_lock-safe: atomic stores only.
    void wdt_pet(hcpp::u8 id) noexcept;

    // Same heartbeat, addressed by name — for call sites that never saw
    // the registration handle. Bounded lookup over max_sources.
    void wdt_pet_name(const char* name) noexcept;

    // True once a log-only source has tripped (selftest observation).
    bool wdt_tripped(hcpp::u8 id) noexcept;

    // Timer-IRQ deadline check: runs on every CPU's vector-32 tick
    // before the scheduler. Panics "watchdog monitor starved" when the
    // monitor thread missed monitor_deadline_ms — i.e. the scheduler
    // itself can no longer dispatch the watchdog. No-op until
    // wdt_initialize() arms the monitor.
    void wdt_irq_check() noexcept;

    // Registers the built-in real sources ("sched", "ui", "input"),
    // spawns the monitor thread, arms the IRQ check, then runs the
    // log-only selftest: registers "wdt-test", pets it once, waits for
    // the monitor to trip it. Serial markers:
    //   "wdt arm=N sources"  — real sources now watched
    //   "wdt pet=ok"         — heartbeat path works
    //   "wdt trip=wdt-test"  — emitted by the monitor (test mode: no panic)
    //   "wdt selftest=ok"    — the test trip was observed, bounded ~4 s
    // Requires sched::scheduler_ready(); without it prints
    // "wdt=offline" and returns false rather than guessing.
    bool wdt_initialize() noexcept;

}  // namespace hype::wdt
