// hypejs — public API, context lifecycle, boot + selftest
// (issue #181).
//
// hjs_create() claims a fixed PMM region (512 KB default) and parks the
// context header at its base; every token, AST node, value, string and
// environment in the engine is bump-allocated behind it. hjs_destroy()
// hands the pages back — that is the entire GC story, honest and
// bounded for a kernel scripting engine.

#include "kernel/hypejs/hjs_int.h"

#include "hcpp/memory.hpp"
#include "kernel/memory/pmm.h"
#include "kernel/serial.hpp"
#include "kernel/vfs/hypefs.h"

namespace hype::hjs {

    namespace {

        constexpr hcpp::u32 k_ctx_align = 16;

        hcpp::u32 str_len(const char* s) noexcept {
            hcpp::u32 n = 0;
            while (s != nullptr && s[n] != '\0') {
                ++n;
            }
            return n;
        }

        bool str_has(const char* hay, const char* needle) noexcept {
            // substring check for selftest error expectations
            for (hcpp::u32 i = 0; hay[i] != '\0'; ++i) {
                hcpp::u32 k = 0;
                while (needle[k] != '\0' && hay[i + k] == needle[k]) {
                    ++k;
                }
                if (needle[k] == '\0') {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    hjs_context* hjs_create(hcpp::u32 arena_bytes) noexcept {
        if (arena_bytes < 64 * 1024) {
            arena_bytes = 64 * 1024;  // floor: header + builtins + room
        }
        const hcpp::u32 pages =
            (arena_bytes + hype::memory::page_size - 1) /
            hype::memory::page_size;
        const hcpp::uintptr_t base =
            hype::memory::pmm_allocate_pages(pages);
        if (base == 0) {
            return nullptr;
        }
        auto* ctx = reinterpret_cast<hjs_context*>(base);
        hcpp::set(ctx, 0, sizeof(*ctx));
        ctx->arena = reinterpret_cast<hcpp::u8*>(base);
        ctx->arena_size = pages * hype::memory::page_size;
        ctx->arena_used =
            (sizeof(hjs_context) + k_ctx_align - 1) & ~(k_ctx_align - 1);
        ctx->step_limit = hjs_steps_default;
        ctx->flow = F_NORMAL;
        ctx->global_env = env_new(*ctx, nullptr);
        if (ctx->global_env == nullptr) {
            hype::memory::pmm_free_pages(base, pages);
            return nullptr;
        }
        hjs_install_builtins(*ctx);
        return ctx;
    }

    void hjs_destroy(hjs_context* ctx) noexcept {
        if (ctx == nullptr) {
            return;
        }
        const hcpp::uintptr_t base =
            reinterpret_cast<hcpp::uintptr_t>(ctx);
        hype::memory::pmm_free_pages(base,
                                     ctx->arena_size /
                                         hype::memory::page_size);
    }

    static void finish(hjs_context& ctx, HjsResult& r,
                   HjsValue v) noexcept {
        if (ctx.flow == F_NORMAL || ctx.flow == F_RETURN) {
            if (ctx.flow == F_RETURN) {
                // Top-level `return` ends the script with a value.
                r.value = ctx.thrown;
                ctx.flow = F_NORMAL;
            } else {
                r.value = v;
            }
            r.ok = true;
            r.error[0] = '\0';
            return;
        }
        r.ok = false;
        r.value = HjsValue::undefined();
        if (ctx.error[0] != '\0') {
            hcpp::u32 i = 0;
            for (; i + 1 < sizeof(r.error) && ctx.error[i] != '\0'; ++i) {
                r.error[i] = ctx.error[i];
            }
            r.error[i] = '\0';
        } else {
            // Uncaught non-Error throw: render the value.
            char tmp[80];
            HjsBuf b{tmp, 0, sizeof(tmp), false};
            tmp[0] = '\0';
            b.s("uncaught: ");
            hjs_write_val(ctx, b, ctx.thrown, 0);
            tmp[b.n] = '\0';
            hcpp::u32 i = 0;
            for (; i + 1 < sizeof(r.error) && tmp[i] != '\0'; ++i) {
                r.error[i] = tmp[i];
            }
            r.error[i] = '\0';
        }
        ctx.flow = F_NORMAL;
    }

    HjsResult hjs_eval(hjs_context* ctx, const char* src,
                       hcpp::u32 src_len) noexcept {
        HjsResult r{};
        if (ctx == nullptr || src == nullptr) {
            r.ok = false;
            const char* m = "bad context/source";
            for (hcpp::u32 i = 0; i + 1 < sizeof(r.error) && m[i]; ++i) {
                r.error[i] = m[i];
            }
            return r;
        }
        if (src_len == 0) {
            src_len = str_len(src);
        }
        // Identifier/property names are slices of the source; copy it
        // into the arena so env bindings stay valid across evals.
        char* copy = static_cast<char*>(hjs_alloc(*ctx, src_len + 1));
        if (copy == nullptr) {
            r.ok = false;
            const char* m = "arena exhausted";
            for (hcpp::u32 i = 0; i + 1 < sizeof(r.error) && m[i]; ++i) {
                r.error[i] = m[i];
            }
            return r;
        }
        if (src_len != 0) {
            hcpp::copy(copy, src, src_len);
        }
        copy[src_len] = '\0';
        ctx->flow = F_NORMAL;
        ctx->thrown = HjsValue::undefined();
        ctx->error[0] = '\0';
        ctx->steps = 0;
        ctx->call_depth = 0;
        ctx->this_val = HjsValue::undefined();
        HjsNode* root = nullptr;
        if (!hjs_parse(*ctx, copy, src_len, &root)) {
            finish(*ctx, r, HjsValue::undefined());
            return r;
        }
        ctx->flow = F_NORMAL;
        ctx->thrown = HjsValue::undefined();
        const HjsValue v = eval_node(*ctx, ctx->global_env, root);
        finish(*ctx, r, v);
        return r;
    }

    HjsResult hjs_call(hjs_context* ctx, const char* name,
                       const HjsValue* args, hcpp::u32 argc) noexcept {
        HjsResult r{};
        if (ctx == nullptr || name == nullptr) {
            r.ok = false;
            return r;
        }
        hcpp::u32 n = str_len(name);
        HjsBind* b = env_lookup(ctx->global_env, name, n);
        if (b == nullptr) {
            r.ok = false;
            HjsBuf e{r.error, 0, sizeof(r.error), false};
            e.s("not found: ");
            e.sn(name, n < 60 ? n : 60);
            return r;
        }
        ctx->flow = F_NORMAL;
        ctx->error[0] = '\0';
        ctx->steps = 0;
        ctx->call_depth = 0;
        const HjsValue v =
            call_value(*ctx, b->v, HjsValue::undefined(), args, argc);
        finish(*ctx, r, v);
        return r;
    }

    void hjs_set_output(hjs_context* ctx, HjsOutputFn fn,
                        void* user) noexcept {
        if (ctx != nullptr) {
            ctx->out = fn;
            ctx->out_user = user;
        }
    }

    void hjs_set_step_limit(hjs_context* ctx, hcpp::u64 steps) noexcept {
        if (ctx != nullptr && steps != 0) {
            ctx->step_limit = steps;
        }
    }

    hcpp::u64 hjs_steps_used(const hjs_context* ctx) noexcept {
        return ctx != nullptr ? ctx->steps : 0;
    }

    hcpp::u32 hjs_arena_used(const hjs_context* ctx) noexcept {
        return ctx != nullptr ? ctx->arena_used : 0;
    }

    // ---- selftest -------------------------------------------------------------------------

    namespace {

        enum CheckKind : hcpp::u8 {
            CK_NUM = 0,   // result is number == expected
            CK_STR,       // ToString(result) == expected
            CK_ERR,       // eval fails and error contains expected
        };

        struct Test {
            const char* src;
            CheckKind kind;
            double num;
            const char* text;
        };

        // clang-format off
        constexpr Test k_tests[] = {
            // arithmetic + precedence + doubles
            {"1 + 2 * 3", CK_NUM, 7, nullptr},
            {"10 / 4", CK_NUM, 2.5, nullptr},
            {"7 % 3", CK_NUM, 1, nullptr},
            {"2 * 3 - 4 + 1e1", CK_NUM, 12, nullptr},
            {"(5 & 3) | (1 << 4)", CK_NUM, 17, nullptr},
            {"0x10 + 1", CK_NUM, 17, nullptr},
            // strings + concat coercion
            {"\"a\" + \"b\" + 1", CK_STR, 0, "ab1"},
            {"1 + \"x\"", CK_STR, 0, "1x"},
            {"\"hello\".toUpperCase()", CK_STR, 0, "HELLO"},
            {"\"hello\".slice(1,3)", CK_STR, 0, "el"},
            {"\"a,b,c\".split(\",\").length", CK_NUM, 3, nullptr},
            {"\"hello\".indexOf(\"l\")", CK_NUM, 2, nullptr},
            {"\"hello\"[1]", CK_STR, 0, "e"},
            {"\"hello\".length", CK_NUM, 5, nullptr},
            // vars + scope
            {"var x=5; let y=2; const z=3; x+y+z", CK_NUM, 10, nullptr},
            // closures (lexical env capture)
            {"function m(){var n=0;return function(){n++;return n;};}"
             "var c=m();c();c();c();", CK_NUM, 3, nullptr},
            // objects + methods + this
            {"var o={x:4,inc:function(){this.x++;return this.x;}};"
             "o.inc();o.inc();o.x", CK_NUM, 6, nullptr},
            {"var o={a:1,b:2};o.a+o.b", CK_NUM, 3, nullptr},
            {"var o={};o.k=9;o.k", CK_NUM, 9, nullptr},
            {"var o={a:1};var h=\"a\" in o;delete o.a;"
             "var h2=\"a\" in o;h&&!h2", CK_NUM, 1, nullptr},
            // arrays
            {"var a=[1,2];a.push(3);a.length*10+a[2]", CK_NUM, 33, nullptr},
            {"var a=[5,6,7];a.pop()+a.length", CK_NUM, 9, nullptr},
            {"var a=[1,2,3];a.join(\"-\")", CK_STR, 0, "1-2-3"},
            {"var a=[9,8];a[0]+a[1]", CK_NUM, 17, nullptr},
            {"var a=[];a[3]=4;a.length", CK_NUM, 4, nullptr},
            {"Array.isArray([1])+Array.isArray({})", CK_NUM, 1, nullptr},
            // control flow
            {"var s=0;for(var i=1;i<=10;i++){s+=i;}s", CK_NUM, 55, nullptr},
            {"var i=0,s=0;while(i<5){s+=i;i++;}s", CK_NUM, 10, nullptr},
            {"var i=0;do{i++;}while(i<3);i", CK_NUM, 3, nullptr},
            {"var s=0;for(var x of [1,2,3,4]){s+=x;}s", CK_NUM, 10, nullptr},
            {"var r=\"\";for(var c of \"ab\"){r+=c;}r", CK_STR, 0, "ab"},
            {"var s=0;for(var i=0;i<10;i++){if(i==3){continue;}"
             "if(i==7){break;}s+=i;}s", CK_NUM, 18, nullptr},
            {"var t=(2>1)?10:20;var u=(2<1)?10:20;t+u", CK_NUM, 30, nullptr},
            {"var v=null;v==undefined", CK_NUM, 1, nullptr},
            {"1==\"1\"?1:0", CK_NUM, 1, nullptr},
            {"1===\"1\"?1:0", CK_NUM, 0, nullptr},
            // functions
            {"function add(a,b){return a+b;}add(3,4)", CK_NUM, 7, nullptr},
            {"var f=function(x){return x*2;};f(21)", CK_NUM, 42, nullptr},
            {"var f=x=>x*2;f(21)", CK_NUM, 42, nullptr},
            {"var g=(a,b)=>a+b;g(3,4)", CK_NUM, 7, nullptr},
            {"function P(x){this.x=x;}var p=new P(9);p.x", CK_NUM, 9, nullptr},
            // typeof
            {"typeof 1+\"|\"+typeof \"s\"+\"|\"+typeof {}"
             "+\"|\"+typeof zzz", CK_STR, 0,
             "number|string|object|undefined"},
            {"typeof (function(){})", CK_STR, 0, "function"},
            // Math
            {"Math.max(3,7)+Math.floor(2.9)+Math.abs(-5)",
             CK_NUM, 14, nullptr},
            {"Math.min(3,7)+Math.ceil(2.1)+Math.round(2.5)",
             CK_NUM, 9, nullptr},
            {"Math.sqrt(9)+Math.pow(2,3)", CK_NUM, 11, nullptr},
            {"Math.floor(Math.PI)+Math.trunc(-1.9)", CK_NUM, 2, nullptr},
            // JSON
            {"var o={a:1,b:[2,3]};var s=JSON.stringify(o);"
             "var p=JSON.parse(s);p.b[1]+p.a*10", CK_NUM, 13, nullptr},
            {"JSON.stringify({a:1,s:\"x\",b:[true,null]})",
             CK_STR, 0, "{\"a\":1,\"s\":\"x\",\"b\":[true,null]}"},
            {"JSON.parse(\"[1,2,3]\")[1]", CK_NUM, 2, nullptr},
            {"JSON.parse(\"{\\\"k\\\":\\\"v\\\"}\").k", CK_STR, 0, "v"},
            // conversions + globals
            {"parseInt(\"42px\")", CK_NUM, 42, nullptr},
            {"isNaN(\"x\")&&!isNaN(\"3\")", CK_NUM, 1, nullptr},
            {"Number(\"3.5\")*2", CK_NUM, 7, nullptr},
            {"String(4)+Boolean(0)", CK_STR, 0, "4false"},
            {"isFinite(1/0)?1:0", CK_NUM, 0, nullptr},
            // try/catch/finally/throw
            {"var r=\"\";try{throw 42;}catch(e){r=\"c\"+e;}r",
             CK_STR, 0, "c42"},
            {"var r=0;try{r=1;}finally{r+=10;}r", CK_NUM, 11, nullptr},
            {"var r=\"\";try{try{throw \"x\";}finally{r+=\"f\";}}"
             "catch(e){r+=e;}r", CK_STR, 0, "fx"},
            // kernel natives
            {"typeof sys.uptime()", CK_STR, 0, "number"},
            {"sys.cpus() >= 1", CK_NUM, 1, nullptr},
            {"typeof sys.version()", CK_STR, 0, "string"},
            {"sys.mem().arenaUsed > 0", CK_NUM, 1, nullptr},
            {"clip.set(\"hjs clip\");clip.get()", CK_STR, 0, "hjs clip"},
            {"var s=fs.stat(\"HYPEOS://U/\");s==null?0:(s.dir?1:0)",
             CK_NUM, 1, nullptr},
            // ---- error paths -------------------------------------------------------------
            {"nosuchvar + 1", CK_ERR, 0, "not defined"},
            {"null.x", CK_ERR, 0, "cannot read"},
            {"throw \"boom\"", CK_ERR, 0, "boom"},
            {"var z=;", CK_ERR, 0, "SyntaxError"},
            {"function f(){return f();}f();", CK_ERR, 0, "call depth"},
            {"[1,2].push()", CK_NUM, 2, nullptr},
        };
        // clang-format on

        bool run_test(const Test& t, bool quiet) noexcept {
            hjs_context* ctx = hjs_create();
            if (ctx == nullptr) {
                return false;
            }
            if (str_has(t.src, "while(true)") ||
                str_has(t.src, "step")) {
                hjs_set_step_limit(ctx, 5000);
            }
            const HjsResult r = hjs_eval(ctx, t.src);
            bool pass = false;
            if (t.kind == CK_ERR) {
                pass = !r.ok && t.text != nullptr &&
                       str_has(r.error, t.text);
            } else if (r.ok) {
                if (t.kind == CK_NUM) {
                    // Bool/string results coerce to number (JS rules).
                    pass = hjs_to_num(r.value) == t.num;
                } else {
                    char buf[160];
                    const hcpp::u32 n =
                        hjs_to_string_buf(ctx, r.value, buf, sizeof(buf));
                    pass = n == str_len(t.text) &&
                           hcpp::compare(buf, t.text, n) == 0;
                }
            }
            if (!pass && !quiet) {
                serial::write("hjs FAIL src=");
                serial::write(t.src);
                serial::write(" ok=");
                serial::write(r.ok ? "1" : "0");
                serial::write(" err=");
                serial::write_line(r.error);
                if (r.ok) {
                    char gb[160];
                    const hcpp::u32 gn = hjs_to_string_buf(
                        ctx, r.value, gb, sizeof(gb));
                    serial::write("hjs FAIL got=");
                    serial::write(hcpp::StringView(gb, gn));
                    serial::write("\r\n");
                }
            }
            hjs_destroy(ctx);
            return pass;
        }

    }  // namespace

    bool hjs_selftest() noexcept {
        hcpp::u32 pass = 0;
        hcpp::u32 fail = 0;
        const hcpp::u32 total =
            sizeof(k_tests) / sizeof(k_tests[0]);
        for (hcpp::u32 i = 0; i < total; ++i) {
            if (run_test(k_tests[i], false)) {
                ++pass;
            } else {
                ++fail;
            }
        }
        serial::write("hjs tests pass=");
        serial::write_dec32(pass);
        serial::write("/");
        serial::write_dec32(total);
        serial::write("\r\n");
        // Step-limit trap needs its own bounded context: a 1M-step
        // default would take too long at selftest time.
        hjs_context* ctx = hjs_create();
        bool limit_ok = false;
        if (ctx != nullptr) {
            hjs_set_step_limit(ctx, 20000);
            const HjsResult r = hjs_eval(ctx, "var i=0;while(true){i++;}");
            limit_ok = !r.ok && str_has(r.error, "step limit");
            hjs_destroy(ctx);
        }
        serial::write("hjs limit=");
        serial::write_line(limit_ok ? "ok" : "fail");
        // fs round-trip through the real VFS + cleanup.
        bool fs_ok = false;
        ctx = hjs_create();
        if (ctx != nullptr) {
            const HjsResult r = hjs_eval(
                ctx,
                "fs.write(\"HYPEOS://U/hjs-probe.txt\",\"hjs io\");"
                "fs.read(\"HYPEOS://U/hjs-probe.txt\")");
            fs_ok = r.ok && r.value.type == HJS_STRING &&
                    str_eq(r.value.u.str->data, r.value.u.str->len,
                           "hjs io", 6);
            hjs_destroy(ctx);
            (void)hype::vfs::fs_delete("HYPEOS://U/hjs-probe.txt");
        }
        serial::write("hjs fs=");
        serial::write_line(fs_ok ? "ok" : "fail");
        const bool ok = fail == 0 && limit_ok && fs_ok;
        serial::write("hjs eval=");
        serial::write_line(ok ? "ok" : "fail");
        serial::write("hjs selftest=");
        serial::write_line(ok ? "ok" : "fail");
        return ok;
    }

    void hjs_boot() noexcept {
        serial::write("hjs engine=tree-walk arena=");
        serial::write_dec32(hjs_arena_default / 1024);
        serial::write("K steps=");
        serial::write_dec32(
            static_cast<hcpp::u32>(hjs_steps_default));
        serial::write("\r\n");
        (void)hjs_selftest();
    }

}  // namespace hype::hjs
