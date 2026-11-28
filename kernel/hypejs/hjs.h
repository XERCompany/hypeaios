#pragma once

// hypejs — HypeAIJS, a JavaScript engine for the HypeAI OS kernel
// (issue #181, "V8 -> HypeAIJS").
//
// Real V8 is not portable here (millions of LOC, JIT, ICU, libc), so
// this is an honest QuickJS/Duktape-class engine written for the
// freestanding kernel: hand-rolled tokenizer, recursive-descent parser
// producing a real AST, and a tree-walking interpreter over arena-heap
// values. ES-subset — doubles via SSE, no GC (the context arena is
// released whole), no JIT, no prototypes.
//
// Safety model: everything is bounded. The context owns one fixed-size
// PMM region (default 512 KB) that holds every token, AST node, value
// and environment; the evaluator counts steps (default 1M) and call
// depth; arena exhaustion, step-limit trips and script errors all
// surface as HjsResult{ok=false,error=...} — a bad script can never
// hang or panic the kernel.

#include "hcpp/types.hpp"

namespace hype::hjs {

    // ---- limits ---------------------------------------------------------------
    constexpr hcpp::u32 hjs_arena_default = 512u * 1024u;   // PMM region
    constexpr hcpp::u64 hjs_steps_default = 1000000;        // eval step cap
    constexpr hcpp::u32 hjs_call_depth_max = 64;            // recursion cap
    constexpr hcpp::u32 hjs_nodes_max = 4096;               // AST nodes / eval
    constexpr hcpp::u32 hjs_native_max = 48;                // native fn slots
    constexpr hcpp::u32 hjs_fs_read_max = 32u * 1024u;      // fs.read cap
    constexpr hcpp::u32 hjs_json_depth_max = 24;

    // ---- values ---------------------------------------------------------------
    // Internal heap records — opaque here, defined in hjs_int.h.
    struct HjsStr;
    struct HjsObj;
    struct HjsArr;
    struct HjsFunc;
    struct HjsNative;
    struct HjsBound;
    struct hjs_context;

    enum HjsType : hcpp::u8 {
        HJS_UNDEFINED = 0,
        HJS_NULL,
        HJS_BOOL,
        HJS_NUMBER,
        HJS_STRING,
        HJS_OBJECT,
        HJS_ARRAY,
        HJS_FUNCTION,
        HJS_NATIVE,    // native fn value (name -> HjsNative)
        HJS_BOUND,     // native fn bound to a receiver (arr.push etc.)
    };

    struct HjsValue {
        HjsType type = HJS_UNDEFINED;
        union {
            bool b;
            double num;
            HjsStr* str;
            HjsObj* obj;
            HjsArr* arr;
            HjsFunc* fn;
            const HjsNative* nat;
            HjsBound* bound;
        } u{};

        static HjsValue undefined() noexcept { return {}; }
        static HjsValue null() noexcept {
            HjsValue v{};
            v.type = HJS_NULL;
            return v;
        }
        static HjsValue boolean(bool x) noexcept {
            HjsValue v{};
            v.type = HJS_BOOL;
            v.u.b = x;
            return v;
        }
        static HjsValue number(double x) noexcept {
            HjsValue v{};
            v.type = HJS_NUMBER;
            v.u.num = x;
            return v;
        }
    };

    // Native signature: self is the receiver (undefined for plain calls,
    // the bound value for arr.push-style methods). Errors are reported
    // via hjs_throw(ctx, ...) — the returned value is ignored then.
    using HjsNativeFn = HjsValue (*)(hjs_context& ctx, HjsValue self,
                                     const HjsValue* args,
                                     hcpp::u32 argc) noexcept;

    // Output sink for print()/console.log()/sys.print(); `user` is the
    // opaque pointer passed to hjs_set_output. Default = COM1 serial.
    using HjsOutputFn = void (*)(const char* text, hcpp::u32 len,
                                 void* user) noexcept;

    struct HjsResult {
        HjsValue value{};
        bool ok = false;
        char error[96]{};
    };

    // ---- context ----------------------------------------------------------------
    // Allocates `arena_bytes` of PMM (rounded up to pages) and installs
    // the builtin library + kernel-native bridge. nullptr on OOM.
    hjs_context* hjs_create(hcpp::u32 arena_bytes = hjs_arena_default) noexcept;
    // Releases the whole PMM region; every value/env inside dies with it.
    void hjs_destroy(hjs_context* ctx) noexcept;

    // Parse + run `src` (len==0 -> NUL-terminated). Globals persist
    // between evals on the same context until the arena fills.
    HjsResult hjs_eval(hjs_context* ctx, const char* src,
                       hcpp::u32 src_len = 0) noexcept;

    // Invokes a global-scope function by name with `argc` argument values.
    HjsResult hjs_call(hjs_context* ctx, const char* name,
                       const HjsValue* args = nullptr,
                       hcpp::u32 argc = 0) noexcept;

    // Exposes `name` to scripts as a native function. The name may be
    // dotted ("sys.foo") to land on an object created on demand.
    bool hjs_register_native(hjs_context* ctx, const char* name,
                             HjsNativeFn fn) noexcept;

    void hjs_set_output(hjs_context* ctx, HjsOutputFn fn,
                      void* user) noexcept;
    void hjs_set_step_limit(hjs_context* ctx, hcpp::u64 steps) noexcept;
    hcpp::u64 hjs_steps_used(const hjs_context* ctx) noexcept;
    hcpp::u32 hjs_arena_used(const hjs_context* ctx) noexcept;

    // Raises a catchable script error from inside a native fn.
    void hjs_throw(hjs_context* ctx, const char* message) noexcept;

    // ---- embedder helpers -------------------------------------------------------
    const char* hjs_type_name(HjsValue v) noexcept;
    double hjs_to_number(HjsValue v) noexcept;
    bool hjs_to_bool(HjsValue v) noexcept;
    // Renders a value like JS ToString() into `out` (NUL-terminated,
    // truncated at cap-1). Returns chars written.
    hcpp::u32 hjs_to_string_buf(hjs_context* ctx, HjsValue v, char* out,
                                hcpp::u32 cap) noexcept;

    // Boot hook (stage_hxrt): prints the engine banner and runs the
    // embedded selftest battery -> "hjs selftest=ok" / "hjs eval=ok".
    void hjs_boot() noexcept;
    bool hjs_selftest() noexcept;

}  // namespace hype::hjs
