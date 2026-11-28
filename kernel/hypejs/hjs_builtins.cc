// hypejs builtins — the script-visible library plus the kernel native
// bridge (issue #181).
//
// Globals installed on every context:
//   print/console.log -> the ctx output sink (COM1 by default)
//   Math.{abs,floor,ceil,round,trunc,sqrt,pow,exp,log,sin,cos,max,min,
//         sign,random} + Math.PI/E   (rng: kernel entropy pool)
//   JSON.parse / JSON.stringify (depth-bounded subset)
//   Array.isArray, parseInt, isNaN, isFinite, Number, String, Boolean
//   NaN, Infinity, globalThis
// Kernel bridge (read-ish + notify, all bounded):
//   sys.uptime/print/version/mem/cpus/ip
//   fs.read/write/stat/list        (<= 32 KiB reads)
//   net.resolve/net.ip
//   clip.set/get                   (system clipboard)
//   notify(title,msg)              (deskbar toast)

#include "kernel/hypejs/hjs_int.h"

#include "hcpp/memory.hpp"
#include "hcpp/rng.hpp"
#include "kernel/arch/x86/smp.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/net/dns.h"
#include "kernel/net/net.h"
#include "kernel/serial.hpp"
#include "kernel/time/timer.h"
#include "kernel/ui/clipboard.h"
#include "kernel/ui/deskbar.h"
#include "kernel/vfs/hypefs.h"
#include "version_generated.h"

extern "C" double floor(double x) noexcept;
extern "C" double fabs(double x) noexcept;
extern "C" double pow(double x, double y) noexcept;
extern "C" double exp(double x) noexcept;
extern "C" double log(double x) noexcept;
extern "C" double sin(double x) noexcept;
extern "C" double cos(double x) noexcept;

namespace hype::hjs {

    namespace {

        // ---- helpers ------------------------------------------------------------

        double arg_num(const HjsValue* args, hcpp::u32 argc, hcpp::u32 i,
                       double dflt = 0.0) noexcept {
            return i < argc ? hjs_to_num(args[i]) : dflt;
        }

        HjsValue arg_str(hjs_context& ctx, const HjsValue* args,
                         hcpp::u32 argc, hcpp::u32 i) noexcept {
            return i < argc ? hjs_to_str(ctx, args[i])
                            : hjs_str(ctx, "", 0);
        }

        void emit(hjs_context& ctx, const char* p, hcpp::u32 n) noexcept {
            if (ctx.out != nullptr) {
                ctx.out(p, n, ctx.out_user);
                return;
            }
            serial::write(hcpp::StringView(p, n));
        }

        void emit_val(hjs_context& ctx, const HjsValue* args,
                      hcpp::u32 argc) noexcept {
            char buf[384];
            for (hcpp::u32 i = 0; i < argc; ++i) {
                if (i != 0) {
                    emit(ctx, " ", 1);
                }
                HjsBuf b{buf, 0, sizeof(buf), false};
                buf[0] = '\0';
                hjs_write_val(ctx, b, args[i], 0);
                emit(ctx, buf, b.n);
            }
            emit(ctx, "\n", 1);
        }

        // ---- print / console --------------------------------------------------------

        HjsValue fn_print(hjs_context& ctx, HjsValue, const HjsValue* args,
                          hcpp::u32 argc) noexcept {
            emit_val(ctx, args, argc);
            return HjsValue::undefined();
        }

        // ---- Math ---------------------------------------------------------------------

        HjsValue math_abs(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::number(fabs(arg_num(a, n, 0)));
        }
        HjsValue math_floor(hjs_context&, HjsValue, const HjsValue* a,
                            hcpp::u32 n) noexcept {
            return HjsValue::number(floor(arg_num(a, n, 0)));
        }
        HjsValue math_ceil(hjs_context&, HjsValue, const HjsValue* a,
                           hcpp::u32 n) noexcept {
            const double v = arg_num(a, n, 0);
            return HjsValue::number(-floor(-v));
        }
        HjsValue math_round(hjs_context&, HjsValue, const HjsValue* a,
                            hcpp::u32 n) noexcept {
            return HjsValue::number(floor(arg_num(a, n, 0) + 0.5));
        }
        HjsValue math_trunc(hjs_context&, HjsValue, const HjsValue* a,
                            hcpp::u32 n) noexcept {
            const double v = arg_num(a, n, 0);
            return HjsValue::number(v < 0 ? -floor(-v) : floor(v));
        }
        HjsValue math_sqrt(hjs_context&, HjsValue, const HjsValue* a,
                           hcpp::u32 n) noexcept {
            return HjsValue::number(pow(arg_num(a, n, 0), 0.5));
        }
        HjsValue math_pow(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::number(pow(arg_num(a, n, 0),
                                        arg_num(a, n, 1)));
        }
        HjsValue math_exp(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::number(exp(arg_num(a, n, 0)));
        }
        HjsValue math_log(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::number(log(arg_num(a, n, 0)));
        }
        HjsValue math_sin(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::number(sin(arg_num(a, n, 0)));
        }
        HjsValue math_cos(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::number(cos(arg_num(a, n, 0)));
        }
        HjsValue math_max(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            double m = n != 0 ? arg_num(a, n, 0) : -__builtin_huge_val();
            for (hcpp::u32 i = 1; i < n; ++i) {
                const double v = arg_num(a, n, i);
                if (num_is_nan(v)) {
                    return HjsValue::number(v);
                }
                if (v > m) {
                    m = v;
                }
            }
            return HjsValue::number(m);
        }
        HjsValue math_min(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            double m = n != 0 ? arg_num(a, n, 0) : __builtin_huge_val();
            for (hcpp::u32 i = 1; i < n; ++i) {
                const double v = arg_num(a, n, i);
                if (num_is_nan(v)) {
                    return HjsValue::number(v);
                }
                if (v < m) {
                    m = v;
                }
            }
            return HjsValue::number(m);
        }
        HjsValue math_sign(hjs_context&, HjsValue, const HjsValue* a,
                           hcpp::u32 n) noexcept {
            const double v = arg_num(a, n, 0);
            if (num_is_nan(v)) {
                return HjsValue::number(v);
            }
            return HjsValue::number(v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0));
        }
        HjsValue math_random(hjs_context&, HjsValue, const HjsValue*,
                             hcpp::u32) noexcept {
            // Kernel entropy pool (SHA-256 DRBG over RDRAND/TSC).
            const hcpp::u64 r = hcpp::rng_u64();
            return HjsValue::number(
                static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0));
        }

        // ---- scalar globals ----------------------------------------------------------

        HjsValue fn_parseint(hjs_context& ctx, HjsValue,
                             const HjsValue* a, hcpp::u32 n) noexcept {
            // JS parseInt: optional ws/sign then a digit prefix — the
            // trailing junk does NOT poison the result like str_to_num.
            if (n == 0) {
                return HjsValue::number(__builtin_nan(""));
            }
            HjsValue s = hjs_to_str(ctx, a[0]);
            if (s.type != HJS_STRING) {
                return HjsValue::number(__builtin_nan(""));
            }
            const char* p = s.u.str->data;
            const hcpp::u32 len = s.u.str->len;
            hcpp::u32 i = 0;
            while (i < len && p[i] == ' ') {
                ++i;
            }
            bool neg = false;
            if (i < len && (p[i] == '+' || p[i] == '-')) {
                neg = p[i] == '-';
                ++i;
            }
            hcpp::i64 v = 0;
            bool any = false;
            while (i < len && p[i] >= '0' && p[i] <= '9' &&
                   v < 0x7FFFFFFFll) {
                v = v * 10 + (p[i] - '0');
                any = true;
                ++i;
            }
            if (!any) {
                return HjsValue::number(__builtin_nan(""));
            }
            return HjsValue::number(
                static_cast<double>(neg ? -v : v));
        }
        HjsValue fn_isnan(hjs_context&, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            return HjsValue::boolean(n != 0 && num_is_nan(hjs_to_num(a[0])));
        }
        HjsValue fn_isfinite(hjs_context&, HjsValue, const HjsValue* a,
                             hcpp::u32 n) noexcept {
            if (n == 0) {
                return HjsValue::boolean(false);
            }
            const double v = hjs_to_num(a[0]);
            return HjsValue::boolean(!num_is_nan(v) && !num_is_nan(v - v));
        }
        HjsValue fn_number(hjs_context&, HjsValue, const HjsValue* a,
                           hcpp::u32 n) noexcept {
            return HjsValue::number(n != 0 ? hjs_to_num(a[0]) : 0.0);
        }
        HjsValue fn_string(hjs_context& ctx, HjsValue, const HjsValue* a,
                           hcpp::u32 n) noexcept {
            return n != 0 ? hjs_to_str(ctx, a[0]) : hjs_str(ctx, "", 0);
        }
        HjsValue fn_boolean(hjs_context&, HjsValue, const HjsValue* a,
                            hcpp::u32 n) noexcept {
            return HjsValue::boolean(n != 0 && hjs_truthy(a[0]));
        }

        // ---- Array ----------------------------------------------------------------------

        HjsValue array_isarray(hjs_context&, HjsValue, const HjsValue* a,
                               hcpp::u32 n) noexcept {
            return HjsValue::boolean(n != 0 && a[0].type == HJS_ARRAY);
        }

        // ---- JSON ------------------------------------------------------------------------

        // Bounded recursive-descent JSON parser producing real values.
        struct JsonParse {
            hjs_context* ctx;
            const char* p;
            hcpp::u32 n;
            hcpp::u32 pos;
            hcpp::u32 depth;
        };

        void jp_ws(JsonParse& j) noexcept {
            while (j.pos < j.n &&
                   (j.p[j.pos] == ' ' || j.p[j.pos] == '\t' ||
                    j.p[j.pos] == '\n' || j.p[j.pos] == '\r')) {
                ++j.pos;
            }
        }

        bool jp_lit(JsonParse& j, const char* lit) noexcept {
            hcpp::u32 i = 0;
            while (lit[i] != '\0' && j.pos + i < j.n &&
                   j.p[j.pos + i] == lit[i]) {
                ++i;
            }
            if (lit[i] != '\0') {
                return false;
            }
            j.pos += i;
            return true;
        }

        HjsValue jp_value(hjs_context& ctx, JsonParse& j) noexcept {
            if (j.depth > hjs_json_depth_max) {
                hjs_err(ctx, "SyntaxError", "JSON too deep");
                return HjsValue::undefined();
            }
            jp_ws(j);
            if (j.pos >= j.n) {
                hjs_err(ctx, "SyntaxError", "unexpected end of JSON");
                return HjsValue::undefined();
            }
            const char c = j.p[j.pos];
            if (c == 'n') {
                if (!jp_lit(j, "null")) {
                    hjs_err(ctx, "SyntaxError", "bad JSON literal");
                    return HjsValue::undefined();
                }
                return HjsValue::null();
            }
            if (c == 't' || c == 'f') {
                const char* lit = c == 't' ? "true" : "false";
                if (!jp_lit(j, lit)) {
                    hjs_err(ctx, "SyntaxError", "bad JSON literal");
                    return HjsValue::undefined();
                }
                return HjsValue::boolean(c == 't');
            }
            if (c == '"') {
                ++j.pos;
                const hcpp::u32 start = j.pos;
                while (j.pos < j.n && j.p[j.pos] != '"') {
                    j.pos += j.p[j.pos] == '\\' ? 2 : 1;
                }
                if (j.pos >= j.n) {
                    hjs_err(ctx, "SyntaxError", "unterminated JSON string");
                    return HjsValue::undefined();
                }
                // Decode escapes into an arena buffer.
                char* buf = static_cast<char*>(
                    hjs_alloc(ctx, j.pos - start + 1));
                if (buf == nullptr) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return HjsValue::undefined();
                }
                hcpp::u32 k = 0;
                for (hcpp::u32 i = start; i < j.pos; ++i) {
                    char ch = j.p[i];
                    if (ch == '\\' && i + 1 < j.pos) {
                        const char e = j.p[++i];
                        switch (e) {
                            case 'n': ch = '\n'; break;
                            case 't': ch = '\t'; break;
                            case 'r': ch = '\r'; break;
                            case 'b': ch = '\b'; break;
                            case 'f': ch = '\f'; break;
                            case '/': ch = '/'; break;
                            case 'u':
                                if (i + 4 < j.pos) {
                                    ch = '?';  // ASCII engine
                                    i += 4;
                                }
                                break;
                            default: ch = e; break;
                        }
                    }
                    buf[k++] = ch;
                }
                ++j.pos;
                return hjs_str(ctx, buf, k);
            }
            if (c == '[') {
                ++j.pos;
                ++j.depth;
                HjsValue arr = hjs_arr(ctx);
                if (arr.type != HJS_ARRAY) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return HjsValue::undefined();
                }
                jp_ws(j);
                if (j.pos < j.n && j.p[j.pos] == ']') {
                    ++j.pos;
                    --j.depth;
                    return arr;
                }
                for (;;) {
                    HjsValue e = jp_value(ctx, j);
                    if (ctx.flow == F_THROW) {
                        return HjsValue::undefined();
                    }
                    if (!hjs_arr_push(ctx, arr.u.arr, e)) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return HjsValue::undefined();
                    }
                    jp_ws(j);
                    if (j.pos < j.n && j.p[j.pos] == ',') {
                        ++j.pos;
                        continue;
                    }
                    if (j.pos < j.n && j.p[j.pos] == ']') {
                        ++j.pos;
                        --j.depth;
                        return arr;
                    }
                    hjs_err(ctx, "SyntaxError", "expected , or ] in JSON");
                    return HjsValue::undefined();
                }
            }
            if (c == '{') {
                ++j.pos;
                ++j.depth;
                HjsValue o = hjs_obj(ctx);
                if (o.type != HJS_OBJECT) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return HjsValue::undefined();
                }
                jp_ws(j);
                if (j.pos < j.n && j.p[j.pos] == '}') {
                    ++j.pos;
                    --j.depth;
                    return o;
                }
                for (;;) {
                    jp_ws(j);
                    if (j.pos >= j.n || j.p[j.pos] != '"') {
                        hjs_err(ctx, "SyntaxError", "expected JSON key");
                        return HjsValue::undefined();
                    }
                    ++j.pos;
                    const hcpp::u32 kstart = j.pos;
                    while (j.pos < j.n && j.p[j.pos] != '"') {
                        ++j.pos;
                    }
                    const hcpp::u32 klen = j.pos - kstart;
                    char* key = hjs_str_copy(ctx, j.p + kstart, klen);
                    if (key == nullptr || j.pos >= j.n) {
                        hjs_err(ctx, "SyntaxError", "bad JSON key");
                        return HjsValue::undefined();
                    }
                    ++j.pos;
                    jp_ws(j);
                    if (j.pos >= j.n || j.p[j.pos] != ':') {
                        hjs_err(ctx, "SyntaxError", "expected : in JSON");
                        return HjsValue::undefined();
                    }
                    ++j.pos;
                    HjsValue v = jp_value(ctx, j);
                    if (ctx.flow == F_THROW) {
                        return HjsValue::undefined();
                    }
                    if (!obj_set(ctx, o.u.obj, key, klen, v)) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return HjsValue::undefined();
                    }
                    jp_ws(j);
                    if (j.pos < j.n && j.p[j.pos] == ',') {
                        ++j.pos;
                        continue;
                    }
                    if (j.pos < j.n && j.p[j.pos] == '}') {
                        ++j.pos;
                        --j.depth;
                        return o;
                    }
                    hjs_err(ctx, "SyntaxError", "expected , or } in JSON");
                    return HjsValue::undefined();
                }
            }
            // Number.
            const hcpp::u32 start = j.pos;
            if (c == '-') {
                ++j.pos;
            }
            while (j.pos < j.n &&
                   ((j.p[j.pos] >= '0' && j.p[j.pos] <= '9') ||
                    j.p[j.pos] == '.' || j.p[j.pos] == 'e' ||
                    j.p[j.pos] == 'E' || j.p[j.pos] == '+' ||
                    j.p[j.pos] == '-')) {
                ++j.pos;
            }
            if (j.pos == start) {
                hjs_err(ctx, "SyntaxError", "bad JSON value");
                return HjsValue::undefined();
            }
            bool okk = false;
            const double v = str_to_num(j.p + start, j.pos - start, okk);
            if (!okk) {
                hjs_err(ctx, "SyntaxError", "bad JSON number");
                return HjsValue::undefined();
            }
            return HjsValue::number(v);
        }

        HjsValue json_parse(hjs_context& ctx, HjsValue,
                            const HjsValue* a, hcpp::u32 n) noexcept {
            if (n == 0) {
                hjs_err(ctx, "SyntaxError", "JSON.parse needs a string");
                return HjsValue::undefined();
            }
            HjsValue s = hjs_to_str(ctx, a[0]);
            if (s.type != HJS_STRING) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            JsonParse j{&ctx, s.u.str->data, s.u.str->len, 0, 0};
            HjsValue v = jp_value(ctx, j);
            if (ctx.flow == F_THROW) {
                return HjsValue::undefined();
            }
            jp_ws(j);
            if (j.pos != j.n) {
                hjs_err(ctx, "SyntaxError", "trailing JSON data");
                return HjsValue::undefined();
            }
            return v;
        }

        HjsValue json_stringify_fn(hjs_context& ctx, HjsValue,
                                   const HjsValue* a,
                                   hcpp::u32 n) noexcept {
            if (n == 0) {
                return HjsValue::undefined();
            }
            char* buf = static_cast<char*>(hjs_alloc(ctx, 4096));
            if (buf == nullptr) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            HjsBuf b{buf, 0, 4096, false};
            buf[0] = '\0';
            if (!json_stringify(ctx, a[0], b, 0)) {
                return HjsValue::undefined();
            }
            if (b.overflow) {
                hjs_err(ctx, "RangeError", "JSON output cap (4 KiB)");
                return HjsValue::undefined();
            }
            return hjs_str(ctx, buf, b.n);
        }

        // ---- array bound methods -----------------------------------------------------------

        HjsArr* self_arr(HjsValue self) noexcept {
            return self.type == HJS_ARRAY ? self.u.arr : nullptr;
        }

        HjsValue am_push(hjs_context& ctx, HjsValue self,
                         const HjsValue* a, hcpp::u32 n) noexcept {
            HjsArr* arr = self_arr(self);
            if (arr == nullptr) {
                hjs_err(ctx, "TypeError", "push on non-array");
                return HjsValue::undefined();
            }
            for (hcpp::u32 i = 0; i < n; ++i) {
                if (!hjs_arr_push(ctx, arr, a[i])) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return HjsValue::undefined();
                }
            }
            return HjsValue::number(static_cast<double>(arr->len));
        }

        HjsValue am_pop(hjs_context&, HjsValue self, const HjsValue*,
                        hcpp::u32) noexcept {
            HjsArr* arr = self_arr(self);
            if (arr == nullptr || arr->len == 0) {
                return HjsValue::undefined();
            }
            return arr->items[--arr->len];
        }

        HjsValue am_join(hjs_context& ctx, HjsValue self,
                         const HjsValue* a, hcpp::u32 n) noexcept {
            HjsArr* arr = self_arr(self);
            if (arr == nullptr) {
                hjs_err(ctx, "TypeError", "join on non-array");
                return HjsValue::undefined();
            }
            HjsValue sep = n != 0 ? hjs_to_str(ctx, a[0])
                                  : hjs_str(ctx, ",", 1);
            char buf[512];
            HjsBuf b{buf, 0, sizeof(buf), false};
            buf[0] = '\0';
            for (hcpp::u32 i = 0; i < arr->len; ++i) {
                if (i != 0 && sep.type == HJS_STRING) {
                    b.sn(sep.u.str->data, sep.u.str->len);
                }
                if (arr->items[i].type != HJS_UNDEFINED &&
                    arr->items[i].type != HJS_NULL) {
                    hjs_write_val(ctx, b, arr->items[i], 0);
                }
            }
            return hjs_str(ctx, buf, b.n);
        }

        HjsValue am_indexof(hjs_context&, HjsValue self,
                            const HjsValue* a, hcpp::u32 n) noexcept {
            HjsArr* arr = self_arr(self);
            if (arr == nullptr || n == 0) {
                return HjsValue::number(-1.0);
            }
            for (hcpp::u32 i = 0; i < arr->len; ++i) {
                if (hjs_strict_eq(arr->items[i], a[0])) {
                    return HjsValue::number(static_cast<double>(i));
                }
            }
            return HjsValue::number(-1.0);
        }

        HjsValue am_slice(hjs_context& ctx, HjsValue self,
                          const HjsValue* a, hcpp::u32 n) noexcept {
            HjsArr* arr = self_arr(self);
            HjsValue out = hjs_arr(ctx);
            if (arr == nullptr || out.type != HJS_ARRAY) {
                hjs_err(ctx, "TypeError", "slice on non-array");
                return HjsValue::undefined();
            }
            hcpp::i64 lo = 0;
            hcpp::i64 hi = arr->len;
            if (n >= 1) {
                lo = static_cast<hcpp::i64>(arg_num(a, n, 0));
                if (lo < 0) {
                    lo += arr->len;
                }
            }
            if (n >= 2) {
                hi = static_cast<hcpp::i64>(arg_num(a, n, 1));
                if (hi < 0) {
                    hi += arr->len;
                }
            }
            for (hcpp::i64 i = lo; i < hi && i < arr->len; ++i) {
                if (i >= 0 &&
                    !hjs_arr_push(ctx, out.u.arr,
                                  arr->items[i])) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return HjsValue::undefined();
                }
            }
            return out;
        }

        HjsValue am_shift(hjs_context&, HjsValue self, const HjsValue*,
                          hcpp::u32) noexcept {
            HjsArr* arr = self_arr(self);
            if (arr == nullptr || arr->len == 0) {
                return HjsValue::undefined();
            }
            const HjsValue v = arr->items[0];
            for (hcpp::u32 i = 1; i < arr->len; ++i) {
                arr->items[i - 1] = arr->items[i];
            }
            --arr->len;
            return v;
        }

        struct ArrMethod {
            const char* name;
            HjsNativeFn fn;
        };

        constexpr ArrMethod k_arr_methods[] = {
            {"push", am_push},   {"pop", am_pop},
            {"join", am_join},   {"indexOf", am_indexof},
            {"slice", am_slice}, {"shift", am_shift},
        };

        // ---- string bound methods ------------------------------------------------------------

        HjsStr* self_str(hjs_context& ctx, HjsValue self) noexcept {
            if (self.type == HJS_STRING) {
                return self.u.str;
            }
            hjs_err(ctx, "TypeError", "string method on non-string");
            return nullptr;
        }

        HjsValue sm_charat(hjs_context& ctx, HjsValue self,
                           const HjsValue* a, hcpp::u32 n) noexcept {
            HjsStr* s = self_str(ctx, self);
            if (s == nullptr) {
                return HjsValue::undefined();
            }
            const hcpp::i64 i = static_cast<hcpp::i64>(arg_num(a, n, 0));
            if (i < 0 || i >= s->len) {
                return hjs_str(ctx, "", 0);
            }
            return hjs_str(ctx, s->data + i, 1);
        }

        HjsValue sm_indexof(hjs_context& ctx, HjsValue self,
                            const HjsValue* a, hcpp::u32 n) noexcept {
            HjsStr* s = self_str(ctx, self);
            if (s == nullptr || n == 0) {
                return HjsValue::number(-1.0);
            }
            HjsValue needle = hjs_to_str(ctx, a[0]);
            if (needle.type != HJS_STRING) {
                return HjsValue::number(-1.0);
            }
            const hcpp::u32 nl = needle.u.str->len;
            if (nl == 0) {
                return HjsValue::number(0.0);
            }
            for (hcpp::u32 i = 0; i + nl <= s->len; ++i) {
                if (str_eq(s->data + i, nl, needle.u.str->data, nl)) {
                    return HjsValue::number(static_cast<double>(i));
                }
            }
            return HjsValue::number(-1.0);
        }

        HjsValue sm_slice(hjs_context& ctx, HjsValue self,
                          const HjsValue* a, hcpp::u32 n) noexcept {
            HjsStr* s = self_str(ctx, self);
            if (s == nullptr) {
                return HjsValue::undefined();
            }
            hcpp::i64 lo = n >= 1
                               ? static_cast<hcpp::i64>(arg_num(a, n, 0))
                               : 0;
            hcpp::i64 hi = n >= 2
                               ? static_cast<hcpp::i64>(arg_num(a, n, 1))
                               : s->len;
            if (lo < 0) {
                lo += s->len;
            }
            if (hi < 0) {
                hi += s->len;
            }
            if (lo < 0) {
                lo = 0;
            }
            if (hi > s->len) {
                hi = s->len;
            }
            if (hi <= lo) {
                return hjs_str(ctx, "", 0);
            }
            return hjs_str(ctx, s->data + lo,
                           static_cast<hcpp::u32>(hi - lo));
        }

        HjsValue sm_case(hjs_context& ctx, HjsValue self, bool upper,
                         const HjsValue*, hcpp::u32) noexcept {
            HjsStr* s = self_str(ctx, self);
            if (s == nullptr) {
                return HjsValue::undefined();
            }
            char* buf = static_cast<char*>(hjs_alloc(ctx, s->len + 1));
            if (buf == nullptr) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            for (hcpp::u32 i = 0; i < s->len; ++i) {
                char c = s->data[i];
                if (upper && c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                } else if (!upper && c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                buf[i] = c;
            }
            return hjs_str(ctx, buf, s->len);
        }

        HjsValue sm_upper(hjs_context& ctx, HjsValue self,
                          const HjsValue* a, hcpp::u32 n) noexcept {
            return sm_case(ctx, self, true, a, n);
        }
        HjsValue sm_lower(hjs_context& ctx, HjsValue self,
                          const HjsValue* a, hcpp::u32 n) noexcept {
            return sm_case(ctx, self, false, a, n);
        }

        HjsValue sm_split(hjs_context& ctx, HjsValue self,
                          const HjsValue* a, hcpp::u32 n) noexcept {
            HjsStr* s = self_str(ctx, self);
            HjsValue out = hjs_arr(ctx);
            if (s == nullptr || out.type != HJS_ARRAY) {
                return HjsValue::undefined();
            }
            HjsValue sep = n != 0 ? hjs_to_str(ctx, a[0]) : HjsValue{};
            if (sep.type != HJS_STRING) {
                (void)hjs_arr_push(ctx, out.u.arr, self);
                return out;
            }
            const hcpp::u32 nl = sep.u.str->len;
            hcpp::u32 start = 0;
            if (nl == 0) {
                for (hcpp::u32 i = 0; i < s->len; ++i) {
                    (void)hjs_arr_push(ctx, out.u.arr,
                                       hjs_str(ctx, s->data + i, 1));
                }
                return out;
            }
            for (hcpp::u32 i = 0; i + nl <= s->len;) {
                if (str_eq(s->data + i, nl, sep.u.str->data, nl)) {
                    (void)hjs_arr_push(
                        ctx, out.u.arr,
                        hjs_str(ctx, s->data + start, i - start));
                    i += nl;
                    start = i;
                } else {
                    ++i;
                }
            }
            (void)hjs_arr_push(ctx, out.u.arr,
                               hjs_str(ctx, s->data + start,
                                       s->len - start));
            return out;
        }

        HjsValue sm_includes(hjs_context& ctx, HjsValue self,
                             const HjsValue* a, hcpp::u32 n) noexcept {
            HjsValue r = sm_indexof(ctx, self, a, n);
            return HjsValue::boolean(r.type == HJS_NUMBER &&
                                     r.u.num >= 0.0);
        }

        constexpr ArrMethod k_str_methods[] = {
            {"charAt", sm_charat},     {"indexOf", sm_indexof},
            {"slice", sm_slice},       {"substring", sm_slice},
            {"toUpperCase", sm_upper}, {"toLowerCase", sm_lower},
            {"split", sm_split},       {"includes", sm_includes},
        };

        // ---- kernel native bridge ---------------------------------------------------------

        HjsValue sys_uptime(hjs_context&, HjsValue, const HjsValue*,
                            hcpp::u32) noexcept {
            return HjsValue::number(
                static_cast<double>(hype::time::uptime_ms()));
        }

        HjsValue sys_version(hjs_context& ctx, HjsValue, const HjsValue*,
                             hcpp::u32) noexcept {
            return hjs_str(ctx, HYPEAI_VERSION_FULL);
        }

        HjsValue sys_mem(hjs_context& ctx, HjsValue, const HjsValue*,
                         hcpp::u32) noexcept {
            const hype::memory::PhysicalMemoryStats st =
                hype::memory::pmm_stats();
            HjsValue o = hjs_obj(ctx);
            if (o.type != HJS_OBJECT) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            (void)obj_set(ctx, o.u.obj, "pmmFree", 7,
                          HjsValue::number(
                              static_cast<double>(st.free_bytes)));
            (void)obj_set(ctx, o.u.obj, "pmmUsed", 7,
                          HjsValue::number(
                              static_cast<double>(st.used_bytes)));
            (void)obj_set(ctx, o.u.obj, "heapUsed", 8,
                          HjsValue::number(static_cast<double>(
                              hype::memory::heap_used_bytes())));
            (void)obj_set(ctx, o.u.obj, "arenaUsed", 9,
                          HjsValue::number(
                              static_cast<double>(ctx.arena_used)));
            return o;
        }

        HjsValue sys_cpus(hjs_context&, HjsValue, const HjsValue*,
                          hcpp::u32) noexcept {
            return HjsValue::number(static_cast<double>(
                hype::x86::smp_cpu_count()));
        }

        void fmt_ip(HjsBuf& b, hcpp::u32 ip) noexcept {
            for (hcpp::i32 i = 0; i < 4; ++i) {
                if (i != 0) {
                    b.ch('.');
                }
                b.dec((ip >> (24 - i * 8)) & 0xFFu);
            }
        }

        HjsValue sys_ip(hjs_context& ctx, HjsValue, const HjsValue*,
                        hcpp::u32) noexcept {
            if (!hype::net::net_ready()) {
                return HjsValue::null();
            }
            char buf[16];
            HjsBuf b{buf, 0, sizeof(buf), false};
            buf[0] = '\0';
            fmt_ip(b, hype::net::net_ip());
            return hjs_str(ctx, buf, b.n);
        }

        // fs.read(path) -> file text or null (bounded 32 KiB).
        HjsValue fs_read(hjs_context& ctx, HjsValue, const HjsValue* a,
                         hcpp::u32 n) noexcept {
            HjsValue path = arg_str(ctx, a, n, 0);
            if (path.type != HJS_STRING || path.u.str->len == 0) {
                hjs_err(ctx, "TypeError", "fs.read needs a path");
                return HjsValue::undefined();
            }
            hype::vfs::FsStat st{};
            if (hype::vfs::fs_stat(path.u.str->data, st) !=
                    hype::vfs::FsStatus::Ok ||
                (st.flags & hype::vfs::FS_DIR) != 0) {
                return HjsValue::null();
            }
            const hcpp::u32 want =
                st.size < hjs_fs_read_max ? st.size : hjs_fs_read_max;
            char* buf = static_cast<char*>(hjs_alloc(ctx, want + 1));
            if (buf == nullptr) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            hcpp::u32 got = 0;
            if (hype::vfs::fs_read(path.u.str->data, buf, want, &got) !=
                hype::vfs::FsStatus::Ok) {
                return HjsValue::null();
            }
            return hjs_str(ctx, buf, got);
        }

        // fs.write(path, text) -> bool.
        HjsValue fs_write(hjs_context& ctx, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            HjsValue path = arg_str(ctx, a, n, 0);
            HjsValue data = arg_str(ctx, a, n, 1);
            if (path.type != HJS_STRING || data.type != HJS_STRING) {
                hjs_err(ctx, "TypeError", "fs.write needs (path, text)");
                return HjsValue::undefined();
            }
            return HjsValue::boolean(
                hype::vfs::fs_write(path.u.str->data, data.u.str->data,
                                    data.u.str->len) ==
                hype::vfs::FsStatus::Ok);
        }

        // fs.stat(path) -> {name,size,disk,dir} or null.
        HjsValue fs_stat(hjs_context& ctx, HjsValue, const HjsValue* a,
                         hcpp::u32 n) noexcept {
            HjsValue path = arg_str(ctx, a, n, 0);
            if (path.type != HJS_STRING) {
                hjs_err(ctx, "TypeError", "fs.stat needs a path");
                return HjsValue::undefined();
            }
            hype::vfs::FsStat st{};
            if (hype::vfs::fs_stat(path.u.str->data, st) !=
                hype::vfs::FsStatus::Ok) {
                return HjsValue::null();
            }
            HjsValue o = hjs_obj(ctx);
            if (o.type != HJS_OBJECT) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            (void)obj_set(ctx, o.u.obj, "name", 4, hjs_str(ctx, st.name));
            (void)obj_set(ctx, o.u.obj, "size", 4,
                          HjsValue::number(static_cast<double>(st.size)));
            char disk[2] = {st.disk, '\0'};
            (void)obj_set(ctx, o.u.obj, "disk", 4, hjs_str(ctx, disk, 1));
            (void)obj_set(ctx, o.u.obj, "dir", 3,
                          HjsValue::boolean(
                              (st.flags & hype::vfs::FS_DIR) != 0));
            return o;
        }

        // fs.list(path) -> [names] or null.
        HjsValue fs_list(hjs_context& ctx, HjsValue, const HjsValue* a,
                         hcpp::u32 n) noexcept {
            HjsValue path = arg_str(ctx, a, n, 0);
            if (path.type != HJS_STRING) {
                hjs_err(ctx, "TypeError", "fs.list needs a path");
                return HjsValue::undefined();
            }
            hype::vfs::FsStat items[32];
            const hcpp::u32 got =
                hype::vfs::fs_list(path.u.str->data, items, 32);
            if (got == 0) {
                return HjsValue::null();
            }
            HjsValue out = hjs_arr(ctx);
            if (out.type != HJS_ARRAY) {
                hjs_err(ctx, "Error", "arena exhausted");
                return HjsValue::undefined();
            }
            for (hcpp::u32 i = 0; i < got; ++i) {
                (void)hjs_arr_push(ctx, out.u.arr, hjs_str(ctx, items[i].name));
            }
            return out;
        }

        // net.resolve(name) -> "a.b.c.d" or null (2s cap, null when no NIC).
        HjsValue net_resolve(hjs_context& ctx, HjsValue, const HjsValue* a,
                             hcpp::u32 n) noexcept {
            HjsValue name = arg_str(ctx, a, n, 0);
            if (name.type != HJS_STRING || name.u.str->len == 0 ||
                name.u.str->len >= 64 || !hype::net::net_ready()) {
                return HjsValue::null();
            }
            hcpp::u32 ip = 0;
            if (!hype::net::dns_resolve(name.u.str->data, &ip, 2000)) {
                return HjsValue::null();
            }
            char buf[16];
            HjsBuf b{buf, 0, sizeof(buf), false};
            buf[0] = '\0';
            fmt_ip(b, ip);
            return hjs_str(ctx, buf, b.n);
        }

        // clip.set(text) -> bool, clip.get() -> string or null.
        HjsValue clip_set(hjs_context& ctx, HjsValue, const HjsValue* a,
                          hcpp::u32 n) noexcept {
            HjsValue s = arg_str(ctx, a, n, 0);
            if (s.type != HJS_STRING) {
                hjs_err(ctx, "TypeError", "clip.set needs a string");
                return HjsValue::undefined();
            }
            hype::ui::clip_set(s.u.str->data, s.u.str->len);
            return HjsValue::boolean(true);
        }

        HjsValue clip_get(hjs_context& ctx, HjsValue, const HjsValue*,
                          hcpp::u32) noexcept {
            hcpp::u32 len = 0;
            const char* text = hype::ui::clip_get(len);
            if (text == nullptr) {
                return HjsValue::null();
            }
            return hjs_str(ctx, text, len);
        }

        // notify(title, msg) -> deskbar toast.
        HjsValue fn_notify(hjs_context& ctx, HjsValue, const HjsValue* a,
                           hcpp::u32 n) noexcept {
            HjsValue title = arg_str(ctx, a, n, 0);
            HjsValue msg = arg_str(ctx, a, n, 1);
            if (title.type != HJS_STRING || msg.type != HJS_STRING) {
                hjs_err(ctx, "TypeError", "notify needs (title, msg)");
                return HjsValue::undefined();
            }
            hype::ui::deskbar_notify(title.u.str->data, msg.u.str->data);
            return HjsValue::boolean(true);
        }

        // ---- registration table --------------------------------------------------------------

        struct GlobalFn {
            const char* name;    // dotted name allowed ("sys.print")
            HjsNativeFn fn;
        };

        constexpr GlobalFn k_globals[] = {
            {"print", fn_print},
            {"console.log", fn_print},
            {"console.warn", fn_print},
            {"console.error", fn_print},
            {"sys.print", fn_print},
            {"sys.uptime", sys_uptime},
            {"sys.version", sys_version},
            {"sys.mem", sys_mem},
            {"sys.cpus", sys_cpus},
            {"sys.ip", sys_ip},
            {"fs.read", fs_read},
            {"fs.write", fs_write},
            {"fs.stat", fs_stat},
            {"fs.list", fs_list},
            {"net.resolve", net_resolve},
            {"net.ip", sys_ip},
            {"clip.set", clip_set},
            {"clip.get", clip_get},
            {"notify", fn_notify},
            {"Math.abs", math_abs},
            {"Math.floor", math_floor},
            {"Math.ceil", math_ceil},
            {"Math.round", math_round},
            {"Math.trunc", math_trunc},
            {"Math.sqrt", math_sqrt},
            {"Math.pow", math_pow},
            {"Math.exp", math_exp},
            {"Math.log", math_log},
            {"Math.sin", math_sin},
            {"Math.cos", math_cos},
            {"Math.max", math_max},
            {"Math.min", math_min},
            {"Math.sign", math_sign},
            {"Math.random", math_random},
            {"JSON.parse", json_parse},
            {"JSON.stringify", json_stringify_fn},
            {"Array.isArray", array_isarray},
            {"parseInt", fn_parseint},
            {"isNaN", fn_isnan},
            {"isFinite", fn_isfinite},
            {"Number", fn_number},
            {"String", fn_string},
            {"Boolean", fn_boolean},
        };

    }  // namespace

    HjsNativeFn str_method(const char* name, hcpp::u32 n) noexcept {
        for (hcpp::u32 i = 0;
             i < sizeof(k_str_methods) / sizeof(k_str_methods[0]); ++i) {
            if (str_eqz(name, n, k_str_methods[i].name)) {
                return k_str_methods[i].fn;
            }
        }
        return nullptr;
    }

    HjsNativeFn arr_method(const char* name, hcpp::u32 n) noexcept {
        for (hcpp::u32 i = 0;
             i < sizeof(k_arr_methods) / sizeof(k_arr_methods[0]); ++i) {
            if (str_eqz(name, n, k_arr_methods[i].name)) {
                return k_arr_methods[i].fn;
            }
        }
        return nullptr;
    }

    bool hjs_register_native(hjs_context* ctx, const char* name,
                             HjsNativeFn fn) noexcept {
        if (ctx == nullptr || name == nullptr || fn == nullptr) {
            return false;
        }
        // Dotted names land on an object in the global env.
        const char* dot = nullptr;
        for (const char* p = name; *p != '\0'; ++p) {
            if (*p == '.') {
                dot = p;
            }
        }
        if (dot == nullptr) {
            hcpp::u32 len = 0;
            while (name[len] != '\0') {
                ++len;
            }
            char* iname = hjs_str_copy(*ctx, name, len);
            if (iname == nullptr) {
                return false;
            }
            const HjsValue v = hjs_native_val(*ctx, iname, fn);
            return v.type == HJS_NATIVE &&
                   env_define(*ctx, ctx->global_env, iname, len, v, true);
        }
        // Split "obj.fn" — create/fetch the object, set the prop. Both
        // name pieces are interned into the arena: env bindings hold
        // bare pointers and must outlive this stack frame.
        char obj_name[32];
        const hcpp::u32 on =
            static_cast<hcpp::u32>(dot - name) < sizeof(obj_name) - 1
                ? static_cast<hcpp::u32>(dot - name)
                : sizeof(obj_name) - 1;
        for (hcpp::u32 i = 0; i < on; ++i) {
            obj_name[i] = name[i];
        }
        obj_name[on] = '\0';
        HjsObj* obj = nullptr;
        HjsBind* b = env_lookup(ctx->global_env, obj_name, on);
        if (b != nullptr && b->v.type == HJS_OBJECT) {
            obj = b->v.u.obj;
        } else if (b == nullptr) {
            HjsValue o = hjs_obj(*ctx);
            char* iname = hjs_str_copy(*ctx, obj_name, on);
            if (o.type != HJS_OBJECT || iname == nullptr ||
                !env_define(*ctx, ctx->global_env, iname, on, o, true)) {
                return false;
            }
            obj = o.u.obj;
        } else {
            return false;  // name taken by a non-object
        }
        const char* fname = dot + 1;
        hcpp::u32 flen = 0;
        while (fname[flen] != '\0') {
            ++flen;
        }
        char* ifname = hjs_str_copy(*ctx, fname, flen);
        if (ifname == nullptr) {
            return false;
        }
        const HjsValue v = hjs_native_val(*ctx, ifname, fn);
        return v.type == HJS_NATIVE && obj_set(*ctx, obj, ifname, flen, v);
    }

    void hjs_install_builtins(hjs_context& ctx) noexcept {
        for (hcpp::u32 i = 0;
             i < sizeof(k_globals) / sizeof(k_globals[0]); ++i) {
            (void)hjs_register_native(&ctx, k_globals[i].name,
                                      k_globals[i].fn);
        }
        // Constants + globalThis.
        (void)env_define(ctx, ctx.global_env, "undefined", 9,
                         HjsValue::undefined(), true);
        (void)env_define(ctx, ctx.global_env, "NaN", 3,
                         HjsValue::number(__builtin_nan("")), true);
        (void)env_define(ctx, ctx.global_env, "Infinity", 8,
                         HjsValue::number(__builtin_huge_val()), true);
        HjsValue g = hjs_obj(ctx);
        ctx.global_obj = g.u.obj;
        (void)env_define(ctx, ctx.global_env, "globalThis", 10, g, true);
        // Math constants as props.
        HjsBind* math = env_lookup(ctx.global_env, "Math", 4);
        if (math != nullptr && math->v.type == HJS_OBJECT) {
            (void)obj_set(ctx, math->v.u.obj, "PI", 2,
                          HjsValue::number(3.141592653589793));
            (void)obj_set(ctx, math->v.u.obj, "E", 1,
                          HjsValue::number(2.718281828459045));
        }
    }

}  // namespace hype::hjs
