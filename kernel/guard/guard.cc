// HypeGuard — security/policy module (issue #114). Single
// implementation TU for the header-only-looking API surface under
// kernel/guard/: ruleset engine (rulez), layered domain stacks
// (landlockstack), the per-app landlock sandbox (landlock), namespace
// classification (guardns), the hook/check pipeline (guardapi), the
// event ring (gcerd), the trace ring (trace), userspace
// self-restrictions (rulesetusr), net-op policy (nfuip) and the C-ABI
// gates the NASM trampolines (mts.S, nfuip.S, nsfguard.asm) call.

#include "kernel/guard/guardapi.h"

#include "hcpp/memory.hpp"
#include "hcpp/text_writer.hpp"
#include "kernel/guard/gcerd.h"
#include "kernel/guard/guardcore.h"
#include "kernel/guard/guardns.h"
#include "kernel/guard/landlock.h"
#include "kernel/guard/mts.h"
#include "kernel/guard/nfuip.h"
#include "kernel/guard/rulesetusr.h"
#include "kernel/guard/ruletrace.h"
#include "kernel/guard/trace.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/vfs/hypefs.h"

namespace hype::guard {
namespace {

    GuardHook hooks[guard_max_hooks];
    hcpp::u32 hook_count;
    GuardStats stats;
    bool initialized;

    // Current-subject stack: slot 0 is always the kernel subject.
    Subject subject_stack[guard_subject_stack_max];
    hcpp::u32 subject_depth;

    GuardApp apps[guard_max_apps];
    hcpp::u32 next_domain_id = 1;
    hcpp::u16 next_rule_id = 1;

    // gcerd ring storage.
    GuardEventRecord gcerd_ring[gcerd_capacity];
    hcpp::u32 gcerd_head;    // next write slot
    hcpp::u64 gcerd_written;

    // trace ring storage.
    GuardTraceEntry trace_ring[guard_trace_capacity];
    hcpp::u32 trace_head;
    hcpp::u64 trace_written;
    bool trace_serial = true;
    GuardTraceLevel trace_min = GuardTraceLevel::Debug;

    // Last ruleset evaluation, for ruletrace_last().
    RuleTrace last_trace{};

    RuleSetUsr usr_records[rulesetusr_max];

    // The net policy domain (single domain — nfuip has no per-app
    // layering yet).
    Rulez net_rules;

    // Namespace stacks the landlock hook consults for subjects that
    // have no app-specific domains (or whose domains sit underneath).
    LandlockStack user_default_stack;   // every UserApp sits under this
    LandlockStack root_stack;           // every SystemApp sits under this

    GuardResult hook_evaluate(GuardOp op, const Subject& subject,
                              const char* object, hcpp::u32 aux) noexcept {
        GuardResult result = GuardResult::Allow;
        for (hcpp::u32 i = 0; i < hook_count; ++i) {
            result = guard_combine(
                result, hooks[i](op, subject, object, aux));
        }
        return result;
    }

    void subject_make(Subject& out, const char* app_name,
                      hcpp::u32 flags) noexcept {
        out = {};
        guardcore_str_copy(out.name, guard_subject_name_size, app_name);
        out.uid = 0;
        out.flags = flags;
    }

}  // namespace

// ---- shared helpers (guardcore.h) ------------------------------------

hcpp::u32 guardcore_str_len(const char* src) noexcept {
    hcpp::u32 n = 0;
    if (src == nullptr) return 0;
    while (src[n] != '\0') ++n;
    return n;
}

hcpp::u32 guardcore_str_copy(char* dest, hcpp::u32 dest_size,
                             const char* src) noexcept {
    hcpp::u32 n = 0;
    if (dest == nullptr || dest_size == 0) return 0;
    if (src != nullptr) {
        while (src[n] != '\0' && n + 1 < dest_size) {
            dest[n] = src[n];
            ++n;
        }
    }
    dest[n] = '\0';
    return n;
}

bool guardcore_str_equal(const char* a, const char* b) noexcept {
    if (a == nullptr || b == nullptr) return a == b;
    hcpp::u32 i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return false;
        ++i;
    }
    return a[i] == b[i];
}

bool guardcore_prefix_match(const char* path, const char* prefix) noexcept {
    if (path == nullptr || prefix == nullptr || prefix[0] == '\0') {
        return false;
    }
    hcpp::u32 i = 0;
    while (prefix[i] != '\0') {
        if (path[i] != prefix[i]) return false;
        ++i;
    }
    return true;
}

hcpp::u32 guardcore_next_domain_id() noexcept { return next_domain_id++; }
hcpp::u16 guardcore_next_rule_id() noexcept { return next_rule_id++; }

// ---- name helpers (guard.h) ------------------------------------------

const char* guard_op_name(GuardOp op) noexcept {
    switch (op) {
        case GuardOp::FsRead:  return "fs_read";
        case GuardOp::FsWrite: return "fs_write";
        case GuardOp::FsExec:  return "fs_exec";
        case GuardOp::Net:     return "net";
        case GuardOp::Svc:     return "svc";
    }
    return "?";
}

const char* guard_result_name(GuardResult result) noexcept {
    switch (result) {
        case GuardResult::Allow: return "allow";
        case GuardResult::Log:   return "log";
        case GuardResult::Deny:  return "deny";
    }
    return "?";
}

// ---- rulez ------------------------------------------------------------

void rulez_clear(Rulez& set) noexcept {
    for (auto& rule : set.rules) rule = {};
    set.count = 0;
    set.unmatched_action = RuleAction::Allow;
}

hcpp::u16 rulez_add(Rulez& set, const char* path_prefix,
                    hcpp::u32 ops_mask, RuleAction action) noexcept {
    if (path_prefix == nullptr || path_prefix[0] == '\0' ||
        ops_mask == 0 || set.count >= rulez_max_rules) {
        return 0;
    }
    if (guardcore_str_len(path_prefix) >= rulez_prefix_size) return 0;
    RulezEntry& entry = set.rules[set.count++];
    entry = {};
    guardcore_str_copy(entry.path_prefix, rulez_prefix_size, path_prefix);
    entry.ops_mask = ops_mask;
    entry.action = action;
    entry.rule_id = guardcore_next_rule_id();
    entry.used = true;
    return entry.rule_id;
}

const RulezEntry* rulez_match(const Rulez& set, const char* path,
                              hcpp::u32 op_bit) noexcept {
    if (path == nullptr || op_bit == 0) return nullptr;
    const RulezEntry* best = nullptr;
    hcpp::u32 best_len = 0;
    for (const RulezEntry& entry : set.rules) {
        if (!entry.used || (entry.ops_mask & op_bit) == 0) continue;
        if (!guardcore_prefix_match(path, entry.path_prefix)) continue;
        const hcpp::u32 len = guardcore_str_len(entry.path_prefix);
        // Longest prefix wins; ties keep the earlier rule (stable).
        if (best == nullptr || len > best_len) {
            best = &entry;
            best_len = len;
        }
    }
    return best;
}

hcpp::u32 rulez_count(const Rulez& set) noexcept { return set.count; }

GuardResult rulez_verdict(const Rulez& set,
                          const RulezEntry* entry) noexcept {
    const RuleAction action =
        entry != nullptr ? entry->action : set.unmatched_action;
    switch (action) {
        case RuleAction::Allow: return GuardResult::Allow;
        case RuleAction::Deny:  return GuardResult::Deny;
        case RuleAction::Audit: return GuardResult::Log;
    }
    return GuardResult::Allow;
}

// ---- landlockstack ----------------------------------------------------

void landlock_stack_clear(LandlockStack& stack) noexcept {
    for (auto& domain : stack.domains) domain = {};
    stack.depth = 0;
}

bool landlock_stack_push(LandlockStack& stack,
                         const Rulez& rules) noexcept {
    if (stack.depth >= landlock_max_domains) return false;
    LandlockDomain& domain = stack.domains[stack.depth++];
    domain = {};
    domain.rules = rules;
    domain.domain_id = guardcore_next_domain_id();
    domain.active = true;
    return true;
}

bool landlock_stack_pop(LandlockStack& stack) noexcept {
    if (stack.depth == 0) return false;
    stack.domains[--stack.depth] = {};
    return true;
}

GuardResult landlock_stack_check(const LandlockStack& stack,
                                 const char* path, hcpp::u32 op_bit,
                                 RuleTrace* trace) noexcept {
    GuardResult result = GuardResult::Allow;
    const LandlockDomain* deciding = nullptr;
    const RulezEntry* deciding_rule = nullptr;
    for (hcpp::u32 i = 0; i < stack.depth; ++i) {
        const LandlockDomain& domain = stack.domains[i];
        if (!domain.active) continue;
        const RulezEntry* entry =
            rulez_match(domain.rules, path, op_bit);
        const GuardResult verdict = rulez_verdict(domain.rules, entry);
        if (static_cast<hcpp::u8>(verdict) >
            static_cast<hcpp::u8>(result)) {
            result = verdict;
            deciding = &domain;
            deciding_rule = entry;
        }
    }
    if (trace != nullptr) {
        *trace = {};
        trace->result = result;
        if (deciding != nullptr) {
            trace->domain_id = deciding->domain_id;
            if (deciding_rule != nullptr) {
                trace->rule_id = deciding_rule->rule_id;
                guardcore_str_copy(trace->matched_prefix,
                                   rulez_prefix_size,
                                   deciding_rule->path_prefix);
            }
        }
    }
    return result;
}

hcpp::u32 landlock_stack_depth(const LandlockStack& stack) noexcept {
    return stack.depth;
}

hcpp::u32 landlock_stack_rule_count(const LandlockStack& stack) noexcept {
    hcpp::u32 total = 0;
    for (hcpp::u32 i = 0; i < stack.depth; ++i) {
        if (stack.domains[i].active) {
            total += rulez_count(stack.domains[i].rules);
        }
    }
    return total;
}

// ---- ruletrace --------------------------------------------------------

const RuleTrace& ruletrace_last() noexcept { return last_trace; }
void ruletrace_set(const RuleTrace& trace) noexcept { last_trace = trace; }
void ruletrace_clear() noexcept { last_trace = {}; }

// ---- gcerd ------------------------------------------------------------

void gcerd_record(GuardOp op, const Subject& subject, const char* object,
                  GuardResult result, hcpp::u32 aux) noexcept {
    GuardEventRecord& rec = gcerd_ring[gcerd_head];
    rec = {};
    rec.op = op;
    rec.result = result;
    rec.aux = aux;
    rec.uid = subject.uid;
    guardcore_str_copy(rec.subject, guard_subject_name_size, subject.name);
    guardcore_str_copy(rec.object, guard_object_size,
                       object != nullptr ? object : "");
    rec.timestamp_ms = time::uptime_ms();
    gcerd_head = (gcerd_head + 1) % gcerd_capacity;
    ++gcerd_written;
}

hcpp::u32 gcerd_count() noexcept {
    return gcerd_written < gcerd_capacity
               ? static_cast<hcpp::u32>(gcerd_written)
               : gcerd_capacity;
}

hcpp::u64 gcerd_total() noexcept { return gcerd_written; }

void gcerd_dump(GcerdDumpCb cb, void* ctx) noexcept {
    if (cb == nullptr) return;
    const hcpp::u32 count = gcerd_count();
    // Oldest first: when the ring is full the oldest entry sits at the
    // head (next overwrite slot); otherwise at index 0.
    const hcpp::u32 start = gcerd_written >= gcerd_capacity
                                ? gcerd_head : 0;
    for (hcpp::u32 i = 0; i < count; ++i) {
        cb(gcerd_ring[(start + i) % gcerd_capacity], ctx);
    }
}

void gcerd_clear() noexcept {
    gcerd_head = 0;
    gcerd_written = 0;
}

// ---- trace ------------------------------------------------------------

void guard_trace(GuardTraceLevel level, const char* msg) noexcept {
    if (static_cast<hcpp::u8>(level) <
        static_cast<hcpp::u8>(trace_min)) {
        return;
    }
    GuardTraceEntry& entry = trace_ring[trace_head];
    entry = {};
    entry.level = level;
    guardcore_str_copy(entry.msg, guard_trace_msg_size,
                       msg != nullptr ? msg : "");
    entry.timestamp_ms = time::uptime_ms();
    trace_head = (trace_head + 1) % guard_trace_capacity;
    ++trace_written;
    if (trace_serial) {
        serial::write("guard trace ");
        serial::write(entry.msg);
        serial::write("\r\n");
    }
}

void guard_trace(const char* msg) noexcept {
    guard_trace(GuardTraceLevel::Info, msg);
}

void guard_trace_set_serial(bool enabled) noexcept { trace_serial = enabled; }
void guard_trace_set_min_level(GuardTraceLevel level) noexcept {
    trace_min = level;
}

hcpp::u32 guard_trace_count() noexcept {
    return trace_written < guard_trace_capacity
               ? static_cast<hcpp::u32>(trace_written)
               : guard_trace_capacity;
}

void guard_trace_dump(GuardTraceCb cb, void* ctx) noexcept {
    if (cb == nullptr) return;
    const hcpp::u32 count = guard_trace_count();
    const hcpp::u32 start = trace_written >= guard_trace_capacity
                                ? trace_head : 0;
    for (hcpp::u32 i = 0; i < count; ++i) {
        cb(trace_ring[(start + i) % guard_trace_capacity], ctx);
    }
}

void guard_trace_clear() noexcept {
    trace_head = 0;
    trace_written = 0;
}

// ---- guardns ----------------------------------------------------------

GuardNamespace guardns_of_name(const char* app_name) noexcept {
    const hcpp::u32 len = guardcore_str_len(app_name);
    // ".hxersys" (8 chars) takes precedence: it ends with "sys" not
    // ".hxer", so check it first.
    if (len >= 8) {
        const char* tail = app_name + len - 8;
        if (guardcore_str_equal(tail, ".hxersys")) {
            return GuardNamespace::SystemApp;
        }
    }
    if (len >= 5) {
        const char* tail = app_name + len - 5;
        if (guardcore_str_equal(tail, ".hxer")) {
            return GuardNamespace::UserApp;
        }
    }
    return GuardNamespace::Kernel;
}

GuardNamespace guardns_of_subject(const Subject& subject) noexcept {
    if ((subject.flags & subject_system) != 0) {
        return GuardNamespace::Kernel;
    }
    return guardns_of_name(subject.name);
}

bool guardns_is_system(const Subject& subject) noexcept {
    return guardns_of_subject(subject) != GuardNamespace::UserApp;
}

const char* guardns_name(GuardNamespace ns) noexcept {
    switch (ns) {
        case GuardNamespace::Kernel:    return "kernel";
        case GuardNamespace::SystemApp: return "hxersys";
        case GuardNamespace::UserApp:   return "hxer";
    }
    return "?";
}

// ---- app registry -----------------------------------------------------

GuardApp* guardcore_app_lookup(const char* app_name) noexcept {
    if (app_name == nullptr || app_name[0] == '\0') return nullptr;
    for (GuardApp& app : apps) {
        if (app.used && guardcore_str_equal(app.name, app_name)) {
            return &app;
        }
    }
    return nullptr;
}

GuardApp* guardcore_app_ensure(const char* app_name) noexcept {
    if (app_name == nullptr || app_name[0] == '\0') return nullptr;
    if (GuardApp* found = guardcore_app_lookup(app_name)) return found;
    for (GuardApp& app : apps) {
        if (app.used) continue;
        app = {};
        guardcore_str_copy(app.name, guard_subject_name_size, app_name);
        app.flags = subject_user;
        landlock_stack_clear(app.landlock);
        app.used = true;
        return &app;
    }
    return nullptr;
}

hcpp::u32 guardcore_app_count() noexcept {
    hcpp::u32 n = 0;
    for (const GuardApp& app : apps) {
        if (app.used) ++n;
    }
    return n;
}

// ---- guardapi ---------------------------------------------------------

void guard_init() noexcept {
    for (auto& hook : hooks) hook = nullptr;
    hook_count = 0;
    stats = {};
    for (auto& app : apps) app = {};
    for (auto& rec : usr_records) rec = {};
    next_domain_id = 1;
    next_rule_id = 1;
    ruletrace_clear();
    gcerd_clear();
    guard_trace_clear();
    landlock_stack_clear(user_default_stack);
    landlock_stack_clear(root_stack);
    rulez_clear(net_rules);
    // Subject stack: slot 0 is the kernel subject — trusted code, hook
    // chain bypassed, everything recorded.
    subject_depth = 1;
    subject_make(subject_stack[0], "kernel", subject_system);
    // Built-in hooks, registration order = evaluation order.
    guard_register_hook(landlock_hook);
    guard_register_hook(nfuip_hook);
    stats.hooks_registered = hook_count;
    // Default policy: denylist-style user domain + root ruleset.
    landlock_install_defaults();
    initialized = true;
}

bool guard_ready() noexcept { return initialized; }

bool guard_register_hook(GuardHook hook) noexcept {
    if (hook == nullptr || hook_count >= guard_max_hooks) return false;
    hooks[hook_count++] = hook;
    stats.hooks_registered = hook_count;
    return true;
}

GuardResult guard_check(GuardOp op, const Subject& subject,
                        const char* object, hcpp::u32 aux) noexcept {
    ++stats.checks;
    GuardResult result = GuardResult::Allow;
    if (guardns_of_subject(subject) == GuardNamespace::Kernel) {
        // Trusted code skips the hook chain entirely — recording and
        // stats still run so the audit trail stays complete.
        ++stats.bypassed;
    } else {
        result = hook_evaluate(op, subject, object, aux);
    }
    switch (result) {
        case GuardResult::Allow: ++stats.allowed; break;
        case GuardResult::Log:   ++stats.logged;  break;
        case GuardResult::Deny:  ++stats.denied;  break;
    }
    stats.domains_active = landlock_domain_count();
    gcerd_record(op, subject, object, result, aux);
    // Serial marker: "guard check=<op> allow|deny". Kernel-bypassed
    // checks skip the line (boot would drown in them); every mediated
    // check reports its verdict.
    if (guardns_of_subject(subject) != GuardNamespace::Kernel) {
        serial::write("guard check=");
        serial::write(guard_op_name(op));
        serial::write(" ");
        serial::write_line(guard_result_name(result));
    }
    if (result == GuardResult::Deny) {
        hcpp::TextWriter line;
        line.append("deny op=");
        line.append(guard_op_name(op));
        line.append(" subj=");
        line.append(subject.name);
        line.append(" obj=");
        line.append(object != nullptr ? object : "?");
        if (last_trace.rule_id != 0) {
            line.append(" rule=");
            line.append_dec32(last_trace.rule_id);
            line.append(" dom=");
            line.append_dec32(last_trace.domain_id);
        }
        guard_trace(GuardTraceLevel::Warn, line.c_str());
    }
    return result;
}

GuardResult guard_check(GuardOp op, const char* object) noexcept {
    return guard_check(op, guard_subject_current(), object, 0);
}

const Subject& guard_subject_current() noexcept {
    return subject_stack[subject_depth - 1];
}

bool guard_subject_enter(const char* app_name) noexcept {
    if (app_name == nullptr || app_name[0] == '\0' ||
        subject_depth >= guard_subject_stack_max) {
        return false;
    }
    const GuardNamespace ns = guardns_of_name(app_name);
    const hcpp::u32 flags =
        ns == GuardNamespace::UserApp ? subject_user : subject_system;
    subject_make(subject_stack[subject_depth], app_name, flags);
    ++subject_depth;
    return true;
}

bool guard_subject_leave() noexcept {
    if (subject_depth <= 1) return false;  // kernel context stays pinned
    --subject_depth;
    subject_stack[subject_depth] = {};
    return true;
}

const GuardStats& guard_stats() noexcept { return stats; }

// ---- landlock ---------------------------------------------------------

GuardOp landlock_guard_op(hcpp::u32 fs_op) noexcept {
    switch (fs_op) {
        case landlock_fs_read:   return GuardOp::FsRead;
        case landlock_fs_exec:   return GuardOp::FsExec;
        case landlock_fs_write:
        case landlock_fs_delete:
        default:                 return GuardOp::FsWrite;
    }
}

bool landlock_apply(const char* app_name, const Rulez& ruleset) noexcept {
    GuardApp* app = guardcore_app_ensure(app_name);
    if (app == nullptr) return false;
    return landlock_stack_push(app->landlock, ruleset);
}

bool landlock_pop(const char* app_name) noexcept {
    GuardApp* app = guardcore_app_lookup(app_name);
    if (app == nullptr) return false;
    return landlock_stack_pop(app->landlock);
}

GuardResult landlock_check(const char* path, hcpp::u32 fs_op) noexcept {
    return guard_check(landlock_guard_op(fs_op),
                       guard_subject_current(), path, fs_op);
}

GuardResult landlock_hook(GuardOp op, const Subject& subject,
                          const char* object, hcpp::u32 aux) noexcept {
    if (object == nullptr) return GuardResult::Allow;
    // fs ops only; the fine-grained op bit arrives via aux (the VFS
    // passes the landlock_fs_* bit), falling back to a coarse mapping
    // for hooks invoked without an fs bit.
    hcpp::u32 fs_bit = aux;
    if (fs_bit == 0) {
        switch (op) {
            case GuardOp::FsRead:  fs_bit = landlock_fs_read; break;
            case GuardOp::FsWrite: fs_bit = landlock_fs_write; break;
            case GuardOp::FsExec:  fs_bit = landlock_fs_exec; break;
            default: return GuardResult::Allow;
        }
    } else if ((fs_bit & landlock_fs_all) == 0) {
        return GuardResult::Allow;
    }
    const GuardNamespace ns = guardns_of_subject(subject);
    RuleTrace trace{};
    GuardResult result = GuardResult::Allow;
    if (ns == GuardNamespace::SystemApp) {
        // .hxersys answer to the root ruleset alone.
        result = landlock_stack_check(root_stack, object, fs_bit, &trace);
    } else if (ns == GuardNamespace::UserApp) {
        // .hxer answer to the user-default domain stacked with their
        // own applied domains; most-restrictive wins across both.
        result = landlock_stack_check(user_default_stack, object,
                                      fs_bit, &trace);
        if (GuardApp* app = guardcore_app_lookup(subject.name)) {
            if (app->landlock.depth != 0) {
                RuleTrace app_trace{};
                const GuardResult app_result = landlock_stack_check(
                    app->landlock, object, fs_bit, &app_trace);
                if (static_cast<hcpp::u8>(app_result) >
                    static_cast<hcpp::u8>(result)) {
                    result = app_result;
                    trace = app_trace;
                }
            }
        }
    }
    ruletrace_set(trace);
    return result;
}

void landlock_install_defaults() noexcept {
    // Demo policy (issue #114): non-system apps may read the system
    // disk but cannot write or delete under HYPEOS://S. Kernel code
    // bypasses the hook chain so OS services keep working.
    Rulez user_defaults;
    rulez_clear(user_defaults);
    rulez_add(user_defaults, "HYPEOS://S",
              landlock_fs_write | landlock_fs_delete, RuleAction::Deny);
    landlock_stack_push(user_default_stack, user_defaults);
    // Root ruleset: empty domain = allow-all. System apps remain
    // mediable without changing the hook code.
    Rulez root;
    rulez_clear(root);
    landlock_stack_push(root_stack, root);
}

LandlockStack& landlock_user_defaults() noexcept { return user_default_stack; }
LandlockStack& landlock_root_ruleset() noexcept { return root_stack; }

hcpp::u32 landlock_rule_count() noexcept {
    hcpp::u32 total = landlock_stack_rule_count(user_default_stack) +
                      landlock_stack_rule_count(root_stack);
    for (const GuardApp& app : apps) {
        if (app.used) total += landlock_stack_rule_count(app.landlock);
    }
    return total;
}

hcpp::u32 landlock_domain_count() noexcept {
    hcpp::u32 total = landlock_stack_depth(user_default_stack) +
                      landlock_stack_depth(root_stack);
    for (const GuardApp& app : apps) {
        if (app.used) total += landlock_stack_depth(app.landlock);
    }
    return total;
}

// ---- rulesetusr -------------------------------------------------------

bool rulesetusr_restrict_self(const char* app_name,
                              const char* path_prefix,
                              hcpp::u32 deny_mask) noexcept {
    if (app_name == nullptr || path_prefix == nullptr ||
        path_prefix[0] == '\0' || (deny_mask & landlock_fs_all) == 0) {
        return false;
    }
    // Only .hxer apps may self-restrict; system apps use the root
    // ruleset and kernel subjects never land here.
    if (guardns_of_name(app_name) != GuardNamespace::UserApp) {
        return false;
    }
    for (RuleSetUsr& rec : usr_records) {
        if (rec.used) continue;
        Rulez single;
        rulez_clear(single);
        const hcpp::u16 rule =
            rulez_add(single, path_prefix, deny_mask, RuleAction::Deny);
        if (rule == 0 || !landlock_apply(app_name, single)) return false;
        rec = {};
        guardcore_str_copy(rec.owner, guard_subject_name_size, app_name);
        guardcore_str_copy(rec.path_prefix, rulez_prefix_size,
                           path_prefix);
        rec.deny_mask = deny_mask;
        rec.rule_id = rule;
        rec.used = true;
        return true;
    }
    return false;
}

hcpp::u32 rulesetusr_count() noexcept {
    hcpp::u32 n = 0;
    for (const RuleSetUsr& rec : usr_records) {
        if (rec.used) ++n;
    }
    return n;
}

void rulesetusr_dump(RuleSetUsrCb cb, void* ctx) noexcept {
    if (cb == nullptr) return;
    for (const RuleSetUsr& rec : usr_records) {
        if (rec.used) cb(rec, ctx);
    }
}

// ---- nfuip ------------------------------------------------------------

Rulez& nfuip_rules() noexcept { return net_rules; }

hcpp::u16 nfuip_add_rule(const char* object_prefix, hcpp::u32 ops_mask,
                         RuleAction action) noexcept {
    return rulez_add(net_rules, object_prefix, ops_mask, action);
}

GuardResult nfuip_check(hcpp::u32 op_bit, hcpp::u16 port) noexcept {
    const hcpp::u32 raw = nfuip_gate(op_bit, port);
    return raw <= static_cast<hcpp::u32>(GuardResult::Deny)
               ? static_cast<GuardResult>(raw)
               : GuardResult::Deny;
}

GuardResult nfuip_hook(GuardOp op, const Subject& subject,
                       const char* object, hcpp::u32 aux) noexcept {
    if (op != GuardOp::Net || object == nullptr) {
        return GuardResult::Allow;
    }
    const hcpp::u32 op_bit = aux != 0 ? aux : nfuip_op_all;
    const RulezEntry* entry = rulez_match(net_rules, object, op_bit);
    const GuardResult result = rulez_verdict(net_rules, entry);
    RuleTrace trace{};
    trace.result = result;
    if (entry != nullptr) {
        trace.rule_id = entry->rule_id;
        trace.domain_id = 0;  // net domain: fixed single-domain stack
        guardcore_str_copy(trace.matched_prefix, rulez_prefix_size,
                           entry->path_prefix);
    }
    ruletrace_set(trace);
    (void)subject;
    return result;
}

}  // namespace hype::guard

// ---- C-ABI gates ------------------------------------------------------

extern "C" hcpp::u32 guard_check_gate(hcpp::u64 op, hcpp::u64 aux,
                                    const char* object) noexcept {
    if (op > static_cast<hcpp::u64>(hype::guard::GuardOp::Svc)) {
        return static_cast<hcpp::u32>(hype::guard::GuardResult::Deny);
    }
    return static_cast<hcpp::u32>(hype::guard::guard_check(
        static_cast<hype::guard::GuardOp>(op),
        hype::guard::guard_subject_current(), object,
        static_cast<hcpp::u32>(aux)));
}

extern "C" hcpp::u32 nfuip_check_c(hcpp::u64 op_bit,
                                 hcpp::u64 port) noexcept {
    // Object string the net rules match against:
    // "net:udp:bind:<port>" / "net:udp:send:<port>". Unknown op bits
    // still check in as "net:udp:op:<port>" so a wildcard rule on
    // "net:udp" covers them.
    char object[32];
    hcpp::u32 n = 0;
    const char* head = "net:udp:";
    while (head[n] != '\0') { object[n] = head[n]; ++n; }
    const char* verb =
        (op_bit & hype::guard::nfuip_op_udp_bind) != 0 ? "bind" :
        (op_bit & hype::guard::nfuip_op_udp_send) != 0 ? "send" : "op";
    for (hcpp::u32 i = 0; verb[i] != '\0' && n + 1 < sizeof(object); ++i) {
        object[n++] = verb[i];
    }
    if (n + 1 < sizeof(object)) object[n++] = ':';
    hcpp::u32 divisor = 10000;
    hcpp::u32 p = static_cast<hcpp::u32>(port) & 0xFFFFu;
    bool started = false;
    while (divisor != 0 && n + 1 < sizeof(object)) {
        const hcpp::u32 digit = p / divisor;
        if (digit != 0 || started || divisor == 1) {
            object[n++] = static_cast<char>('0' + digit);
            started = true;
        }
        p %= divisor;
        divisor /= 10;
    }
    object[n] = '\0';
    return static_cast<hcpp::u32>(hype::guard::guard_check(
        hype::guard::GuardOp::Net,
        hype::guard::guard_subject_current(), object,
        static_cast<hcpp::u32>(op_bit)));
}

// ---- selftest + runtime entry ----------------------------------------

namespace hype::guard {

bool guard_selftest() noexcept {
    bool ok = true;
    // 1. mts.S: save a live frame, verify the captured context fields.
    MtsFrame64 frame{};
    mts_save_state(&frame, 0x21, 0);
    const bool mts_ok = frame.rip != 0 && frame.cs != 0 &&
                        frame.rflags != 0 && frame.rsp != 0;
    ok = ok && mts_ok;
    // 2. Apply a deny-write domain to a .hxer test app.
    Rulez app_rules;
    rulez_clear(app_rules);
    const hcpp::u16 rule = rulez_add(
        app_rules, "HYPEOS://S",
        landlock_fs_write | landlock_fs_delete, RuleAction::Deny);
    ok = ok && rule != 0 &&
         landlock_apply("test-app.hxer", app_rules);
    // 3. Enter the app context and probe through the real VFS path:
    // the fs_write choke point must deny HYPEOS://S and pass
    // HYPEOS://U (which then fails inside the fs for unrelated
    // reasons — anything but AccessDenied counts as "not denied").
    ok = ok && guard_subject_enter("test-app.hxer");
    const char probe[] = "x";
    const vfs::FsStatus denied_status =
        vfs::fs_write("HYPEOS://S/guard-probe.bin", probe, 1);
    ok = ok && denied_status == vfs::FsStatus::AccessDenied;
    const vfs::FsStatus allowed_status =
        vfs::fs_write("HYPEOS://U/guard-probe.bin", probe, 1);
    ok = ok && allowed_status != vfs::FsStatus::AccessDenied;
    // 4. Userspace self-restriction: the app narrows its own U: paths.
    ok = ok && rulesetusr_restrict_self("test-app.hxer",
                                        "HYPEOS://U/secret",
                                        landlock_fs_read);
    char read_buf[4]{};
    const vfs::FsStatus usr_status = vfs::fs_read(
        "HYPEOS://U/secret/file.txt", read_buf, sizeof(read_buf),
        nullptr);
    ok = ok && usr_status == vfs::FsStatus::AccessDenied;
    // 5. Net gate through the asm skeleton (nfuip_gate -> nfuip_check_c
    //    -> guard_check): empty net domain means allow.
    ok = ok && nfuip_gate(nfuip_op_udp_send, 8080) ==
                   static_cast<hcpp::u32>(GuardResult::Allow);
    ok = ok && guard_subject_leave();
    // 6. Namespace-guard trampoline as the kernel subject: bypassed
    //    hooks -> allow.
    ok = ok && nsfguard_entry(static_cast<hcpp::u64>(GuardOp::FsWrite),
                              landlock_fs_write,
                              "HYPEOS://S/guard-probe.bin") ==
                   static_cast<hcpp::u32>(GuardResult::Allow);
    serial::write("guard selftest=");
    serial::write_line(ok ? "ok" : "fail");
    return ok;
}

bool guard_runtime_init() noexcept {
    guard_init();
    serial::write("guard core=");
    serial::write(initialized ? "ok" : "fail");
    serial::write(" hooks=");
    serial::write_dec32(stats.hooks_registered);
    serial::write("\r\n");
    serial::write("landlock rules=");
    serial::write_dec32(landlock_rule_count());
    serial::write("\r\n");
    const bool ok = guard_selftest();
    serial::write("guard domains=");
    serial::write_dec32(landlock_domain_count());
    serial::write(" gcerd=");
    serial::write_dec32(gcerd_count());
    serial::write("\r\n");
    return ok;
}

}  // namespace hype::guard
