// hypejs core — arena, value constructors, environments, properties,
// conversions and number<->string formatting (issue #181).

#include "kernel/hypejs/hjs_int.h"

#include "hcpp/memory.hpp"
#include "kernel/serial.hpp"

extern "C" double floor(double x) noexcept;
extern "C" double fabs(double x) noexcept;
extern "C" double pow(double x, double y) noexcept;
extern "C" double log(double x) noexcept;

namespace hype::hjs {

    // ---- arena ----------------------------------------------------------------------

    void* hjs_alloc(hjs_context& ctx, hcpp::u32 bytes) noexcept {
        const hcpp::u32 align = 8;
        const hcpp::u32 used = (ctx.arena_used + align - 1) & ~(align - 1);
        if (bytes == 0 || used + bytes > ctx.arena_size) {
            return nullptr;
        }
        ctx.arena_used = used + bytes;
        return ctx.arena + used;
    }

    char* hjs_str_copy(hjs_context& ctx, const char* p, hcpp::u32 n) noexcept {
        char* d = static_cast<char*>(hjs_alloc(ctx, n + 1));
        if (d == nullptr) {
            return nullptr;
        }
        if (p != nullptr && n != 0) {
            hcpp::copy(d, p, n);
        }
        d[n] = '\0';
        return d;
    }

    // ---- constructors -----------------------------------------------------------------

    HjsValue hjs_str(hjs_context& ctx, const char* p, hcpp::u32 n) noexcept {
        auto* s = static_cast<HjsStr*>(hjs_alloc(ctx, sizeof(HjsStr) + n + 1));
        if (s == nullptr) {
            return HjsValue::undefined();
        }
        s->len = n;
        if (p != nullptr && n != 0) {
            hcpp::copy(s->data, p, n);
        }
        s->data[n] = '\0';
        HjsValue v{};
        v.type = HJS_STRING;
        v.u.str = s;
        return v;
    }

    HjsValue hjs_str(hjs_context& ctx, const char* p) noexcept {
        hcpp::u32 n = 0;
        while (p != nullptr && p[n] != '\0') {
            ++n;
        }
        return hjs_str(ctx, p, n);
    }

    HjsValue hjs_obj(hjs_context& ctx) noexcept {
        auto* o = static_cast<HjsObj*>(hjs_alloc(ctx, sizeof(HjsObj)));
        if (o == nullptr) {
            return HjsValue::undefined();
        }
        o->props = nullptr;
        HjsValue v{};
        v.type = HJS_OBJECT;
        v.u.obj = o;
        return v;
    }

    HjsValue hjs_arr(hjs_context& ctx) noexcept {
        auto* a = static_cast<HjsArr*>(hjs_alloc(ctx, sizeof(HjsArr)));
        if (a == nullptr) {
            return HjsValue::undefined();
        }
        a->items = nullptr;
        a->len = 0;
        a->cap = 0;
        HjsValue v{};
        v.type = HJS_ARRAY;
        v.u.arr = a;
        return v;
    }

    bool hjs_arr_push(hjs_context& ctx, HjsArr* a, HjsValue v) noexcept {
        if (a == nullptr) {
            return false;
        }
        if (a->len == a->cap) {
            const hcpp::u32 cap = a->cap == 0 ? 4 : a->cap * 2;
            if (cap > 4096) {
                return false;  // bounded arrays
            }
            auto* items = static_cast<HjsValue*>(
                hjs_alloc(ctx, cap * sizeof(HjsValue)));
            if (items == nullptr) {
                return false;
            }
            if (a->items != nullptr) {
                hcpp::copy(items, a->items, a->len * sizeof(HjsValue));
            }
            a->items = items;
            a->cap = cap;
        }
        a->items[a->len++] = v;
        return true;
    }

    HjsValue hjs_native_val(hjs_context& ctx, const char* name,
                            HjsNativeFn fn) noexcept {
        if (ctx.native_count >= hjs_native_max) {
            return HjsValue::undefined();
        }
        HjsNative& rec = ctx.natives[ctx.native_count++];
        rec.name = name;
        rec.fn = fn;
        HjsValue v{};
        v.type = HJS_NATIVE;
        v.u.nat = &rec;
        return v;
    }

    HjsValue hjs_bound_val(hjs_context& ctx, HjsNativeFn fn,
                           HjsValue self) noexcept {
        auto* b = static_cast<HjsBound*>(hjs_alloc(ctx, sizeof(HjsBound)));
        if (b == nullptr) {
            return HjsValue::undefined();
        }
        b->fn = fn;
        b->self = self;
        HjsValue v{};
        v.type = HJS_BOUND;
        v.u.bound = b;
        return v;
    }

    // ---- small string utils -------------------------------------------------------------

    bool str_eq(const char* ap, hcpp::u32 an, const char* bp,
                hcpp::u32 bn) noexcept {
        if (an != bn) {
            return false;
        }
        for (hcpp::u32 i = 0; i < an; ++i) {
            if (ap[i] != bp[i]) {
                return false;
            }
        }
        return true;
    }

    bool str_eqz(const char* ap, hcpp::u32 an, const char* bz) noexcept {
        hcpp::u32 i = 0;
        while (i < an && bz[i] != '\0' && ap[i] == bz[i]) {
            ++i;
        }
        return i == an && bz[i] == '\0';
    }

    // ---- errors ---------------------------------------------------------------------------

    void hjs_err(hjs_context& ctx, const char* kind,
                 const char* msg) noexcept {
        if (ctx.error[0] == '\0') {
            HjsBuf b{ctx.error, 0, sizeof(ctx.error), false};
            b.s(kind);
            b.s(": ");
            b.s(msg);
            ctx.error[b.n] = '\0';
        }
        if (ctx.flow != F_THROW) {
            ctx.flow = F_THROW;
            char m[64];
            hcpp::u32 i = 0;
            for (; i + 1 < sizeof(m) && msg[i] != '\0'; ++i) {
                m[i] = msg[i];
            }
            m[i] = '\0';
            ctx.thrown = hjs_str(ctx, m, i);
        }
    }

    void hjs_err_at(hjs_context& ctx, const char* kind, const char* msg,
                    const char* name, hcpp::u32 name_len) noexcept {
        char full[96];
        HjsBuf b{full, 0, sizeof(full), false};
        b.sn(name, name_len);
        b.s(msg);
        full[b.n] = '\0';
        hjs_err(ctx, kind, full);
    }

    void hjs_throw(hjs_context* ctx, const char* message) noexcept {
        if (ctx != nullptr) {
            hjs_err(*ctx, "Error", message);
        }
    }

    // ---- environments -----------------------------------------------------------------------

    HjsEnv* env_new(hjs_context& ctx, HjsEnv* parent) noexcept {
        auto* e = static_cast<HjsEnv*>(hjs_alloc(ctx, sizeof(HjsEnv)));
        if (e == nullptr) {
            return nullptr;
        }
        e->parent = parent;
        e->binds = nullptr;
        return e;
    }

    bool env_define(hjs_context& ctx, HjsEnv* env, const char* name,
                    hcpp::u32 n, HjsValue v, bool is_const) noexcept {
        if (env == nullptr) {
            return false;
        }
        // Redeclaring in the same env just replaces the binding.
        for (HjsBind* b = env->binds; b != nullptr; b = b->next) {
            if (str_eq(b->name, b->name_len, name, n)) {
                b->v = v;
                b->is_const = is_const;
                return true;
            }
        }
        auto* b = static_cast<HjsBind*>(hjs_alloc(ctx, sizeof(HjsBind)));
        if (b == nullptr) {
            return false;
        }
        b->next = env->binds;
        b->name = name;
        b->name_len = n;
        b->v = v;
        b->is_const = is_const;
        env->binds = b;
        return true;
    }

    HjsBind* env_lookup(HjsEnv* env, const char* name,
                        hcpp::u32 n) noexcept {
        for (HjsEnv* e = env; e != nullptr; e = e->parent) {
            for (HjsBind* b = e->binds; b != nullptr; b = b->next) {
                if (str_eq(b->name, b->name_len, name, n)) {
                    return b;
                }
            }
        }
        return nullptr;
    }

    bool env_assign(hjs_context& ctx, HjsEnv* env, const char* name,
                    hcpp::u32 n, HjsValue v) noexcept {
        HjsBind* b = env_lookup(env, name, n);
        if (b == nullptr) {
            // Sloppy mode: assignment to a fresh name creates a global.
            return env_define(ctx, ctx.global_env, name, n, v, false);
        }
        if (b->is_const) {
            hjs_err_at(ctx, "TypeError", " assignment to const", name, n);
            return false;
        }
        b->v = v;
        return true;
    }

    // ---- object properties ------------------------------------------------------------------

    HjsProp* obj_find(HjsObj* o, const char* key, hcpp::u32 n) noexcept {
        for (HjsProp* p = o != nullptr ? o->props : nullptr; p != nullptr;
             p = p->next) {
            if (str_eq(p->key, p->key_len, key, n)) {
                return p;
            }
        }
        return nullptr;
    }

    bool obj_set(hjs_context& ctx, HjsObj* o, const char* key, hcpp::u32 n,
                 HjsValue v) noexcept {
        HjsProp* p = obj_find(o, key, n);
        if (p != nullptr) {
            p->v = v;
            return true;
        }
        auto* np = static_cast<HjsProp*>(hjs_alloc(ctx, sizeof(HjsProp)));
        if (np == nullptr) {
            return false;
        }
        np->key = key;
        np->key_len = n;
        np->v = v;
        np->next = o->props;
        o->props = np;
        return true;
    }

    bool obj_del(HjsObj* o, const char* key, hcpp::u32 n) noexcept {
        HjsProp* prev = nullptr;
        for (HjsProp* p = o != nullptr ? o->props : nullptr; p != nullptr;
             p = p->next) {
            if (str_eq(p->key, p->key_len, key, n)) {
                if (prev != nullptr) {
                    prev->next = p->next;
                } else {
                    o->props = p->next;
                }
                return true;
            }
            prev = p;
        }
        return false;
    }

    // ---- conversions ---------------------------------------------------------------------------

    bool num_is_nan(double x) noexcept { return x != x; }

    bool num_is_int(double x, hcpp::i64& out) noexcept {
        if (num_is_nan(x) || x > 9.0e18 || x < -9.0e18 ||
            x != static_cast<double>(static_cast<hcpp::i64>(x))) {
            return false;
        }
        out = static_cast<hcpp::i64>(x);
        return true;
    }

    static double d_trunc(double x) noexcept {
        if (num_is_nan(x) || x > 9.0e18 || x < -9.0e18) {
            return x;
        }
        return static_cast<double>(static_cast<hcpp::i64>(x));
    }

    double hjs_to_num(HjsValue v) noexcept {
        switch (v.type) {
            case HJS_NUMBER:
                return v.u.num;
            case HJS_BOOL:
                return v.u.b ? 1.0 : 0.0;
            case HJS_NULL:
            case HJS_UNDEFINED:
                return v.type == HJS_NULL ? 0.0 : __builtin_nan("");
            case HJS_STRING: {
                bool ok = false;
                const double r = str_to_num(v.u.str->data, v.u.str->len, ok);
                return ok ? r : __builtin_nan("");
            }
            case HJS_ARRAY: {
                const HjsArr* a = v.u.arr;
                if (a == nullptr || a->len == 0) {
                    return 0.0;
                }
                if (a->len == 1) {
                    return hjs_to_num(a->items[0]);
                }
                return __builtin_nan("");
            }
            default:
                return __builtin_nan("");
        }
    }

    bool hjs_to_bool(HjsValue v) noexcept { return hjs_truthy(v); }

    bool hjs_truthy(HjsValue v) noexcept {
        switch (v.type) {
            case HJS_UNDEFINED:
            case HJS_NULL:
                return false;
            case HJS_BOOL:
                return v.u.b;
            case HJS_NUMBER:
                return v.u.num != 0.0 && !num_is_nan(v.u.num);
            case HJS_STRING:
                return v.u.str != nullptr && v.u.str->len != 0;
            default:
                return true;  // objects/arrays/functions are truthy
        }
    }

    bool hjs_strict_eq(HjsValue a, HjsValue b) noexcept {
        if (a.type != b.type) {
            return false;
        }
        switch (a.type) {
            case HJS_UNDEFINED:
            case HJS_NULL:
                return true;
            case HJS_BOOL:
                return a.u.b == b.u.b;
            case HJS_NUMBER:
                return a.u.num == b.u.num;
            case HJS_STRING:
                return str_eq(a.u.str->data, a.u.str->len, b.u.str->data,
                              b.u.str->len);
            default:
                return a.u.obj == b.u.obj;  // reference identity
        }
    }

    bool hjs_loose_eq(hjs_context& ctx, HjsValue a, HjsValue b) noexcept {
        (void)ctx;
        if (a.type == b.type) {
            return hjs_strict_eq(a, b);
        }
        const bool a_nil =
            a.type == HJS_UNDEFINED || a.type == HJS_NULL;
        const bool b_nil =
            b.type == HJS_UNDEFINED || b.type == HJS_NULL;
        if (a_nil || b_nil) {
            return a_nil && b_nil;
        }
        if (a.type == HJS_NUMBER &&
            (b.type == HJS_STRING || b.type == HJS_BOOL)) {
            return a.u.num == hjs_to_num(b);
        }
        if (b.type == HJS_NUMBER &&
            (a.type == HJS_STRING || a.type == HJS_BOOL)) {
            return hjs_to_num(a) == b.u.num;
        }
        if (a.type == HJS_STRING && b.type == HJS_BOOL) {
            return hjs_to_num(a) == hjs_to_num(b);
        }
        if (a.type == HJS_BOOL && b.type == HJS_STRING) {
            return hjs_to_num(a) == hjs_to_num(b);
        }
        return false;
    }

    // ---- number -> string ----------------------------------------------------------------------
    // Mirrors the JS Number#toString shape: integers print without a
    // decimal point, fixed notation for |v| in [1e-6, 1e18), e-notation
    // outside that. Bounded, no libm printf.

    static void fmt_sci(double v, char* out, hcpp::u32 cap,
                        hcpp::u32& n) noexcept {
        (void)cap;
        // v != 0, sign already emitted; produces d.ddddde±E at out[n..].
        double e = floor(log(v) / 2.302585092994046);
        hcpp::i32 ex = static_cast<hcpp::i32>(e);
        double m = v / pow(10.0, e);
        if (m >= 10.0) {
            m /= 10.0;
            ++ex;
        } else if (m < 1.0) {
            m *= 10.0;
            --ex;
        }
        // 6 significant decimals on the mantissa, strip trailing zeros.
        hcpp::u32 digit = static_cast<hcpp::u32>(m);
        const hcpp::u32 lead_pos = n;
        out[n++] = static_cast<char>('0' + digit);
        m = (m - static_cast<double>(digit)) * 10.0;
        char frac[7];
        hcpp::u32 fn = 0;
        for (hcpp::u32 i = 0; i < 6; ++i) {
            hcpp::u32 d = static_cast<hcpp::u32>(m + 0.0000001);
            frac[fn++] = static_cast<char>('0' + d);
            m = (m - static_cast<double>(d)) * 10.0;
            if (fn >= 4 && m < 0.5) {
                break;
            }
        }
        // Round when the next digit would be >= 5 (carry can ripple into
        // the leading digit; 9.99999e0 -> 1e1 loses exactness, accepted).
        if (m >= 5.0) {
            bool carry = true;
            for (hcpp::i32 i = static_cast<hcpp::i32>(fn) - 1; i >= 0;
                 --i) {
                if (frac[i] != '9') {
                    ++frac[i];
                    carry = false;
                    break;
                }
                frac[i] = '0';
            }
            if (carry && out[lead_pos] != '9') {
                ++out[lead_pos];
            }
        }
        while (fn != 0 && frac[fn - 1] == '0') {
            --fn;
        }
        if (fn != 0) {
            out[n++] = '.';
            for (hcpp::u32 i = 0; i < fn; ++i) {
                out[n++] = frac[i];
            }
        }
        out[n++] = 'e';
        out[n++] = ex < 0 ? '-' : '+';
        hcpp::u32 a = static_cast<hcpp::u32>(ex < 0 ? -ex : ex);
        char tmp[4];
        hcpp::u32 tn = 0;
        do {
            tmp[tn++] = static_cast<char>('0' + (a % 10));
            a /= 10;
        } while (a != 0 && tn < sizeof(tmp));
        while (tn != 0) {
            out[n++] = tmp[--tn];
        }
        out[n] = '\0';
    }

    hcpp::u32 num_to_str(double v, char* out, hcpp::u32 cap) noexcept {
        if (cap < 8) {
            return 0;
        }
        hcpp::u32 n = 0;
        if (num_is_nan(v)) {
            hcpp::copy(out, "NaN", 4);
            return 3;
        }
        if (num_is_nan(v - v)) {  // +-Infinity (v - v is NaN for inf)
            if (v < 0) {
                out[n++] = '-';
            }
            const char* s = "Infinity";
            for (hcpp::u32 i = 0; s[i] != '\0'; ++i) {
                out[n++] = s[i];
            }
            out[n] = '\0';
            return n;
        }
        if (v == 0.0) {
            hcpp::copy(out, "0", 2);
            return 1;
        }
        if (v < 0) {
            out[n++] = '-';
            v = -v;
        }
        if (v >= 1.0e18 || v < 1.0e-6) {
            fmt_sci(v, out, cap, n);
            return n;
        }
        // Fixed notation: integer part then up to 8 fraction digits.
        const double ip = floor(v);
        hcpp::u64 whole = static_cast<hcpp::u64>(ip);
        char tmp[24];
        hcpp::u32 tn = 0;
        do {
            tmp[tn++] = static_cast<char>('0' + (whole % 10));
            whole /= 10;
        } while (whole != 0 && tn < sizeof(tmp));
        while (tn != 0 && n + 1 < cap) {
            out[n++] = tmp[--tn];
        }
        double f = v - ip;
        if (f > 0.000000005) {
            char frac[9];
            hcpp::u32 fn = 0;
            for (hcpp::u32 i = 0; i < 8 && f > 0.000000005; ++i) {
                f *= 10.0;
                const hcpp::u32 d = static_cast<hcpp::u32>(f);
                frac[fn++] = static_cast<char>('0' + d);
                f -= static_cast<double>(d);
            }
            if (f >= 0.5) {  // round last emitted digit
                for (hcpp::i32 i = static_cast<hcpp::i32>(fn) - 1; i >= 0;
                     --i) {
                    if (frac[i] == '9') {
                        frac[i] = '0';
                    } else {
                        ++frac[i];
                        break;
                    }
                }
            }
            while (fn != 0 && frac[fn - 1] == '0') {
                --fn;
            }
            if (fn != 0 && n + 1 < cap) {
                out[n++] = '.';
                for (hcpp::u32 i = 0; i < fn && n + 1 < cap; ++i) {
                    out[n++] = frac[i];
                }
            }
        }
        out[n] = '\0';
        return n;
    }

    // ---- string -> number ----------------------------------------------------------------------

    double str_to_num(const char* p, hcpp::u32 n, bool& ok) noexcept {
        hcpp::u32 i = 0;
        while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' ||
                         p[i] == '\r')) {
            ++i;
        }
        bool neg = false;
        if (i < n && (p[i] == '+' || p[i] == '-')) {
            neg = p[i] == '-';
            ++i;
        }
        if (i < n && p[i] == 'I') {  // Infinity
            if (i + 8 <= n && str_eq(p + i, 8, "Infinity", 8)) {
                ok = true;
                return neg ? -__builtin_huge_val() : __builtin_huge_val();
            }
        }
        double v = 0.0;
        bool any = false;
        if (i + 1 < n && p[i] == '0' && (p[i + 1] == 'x' || p[i + 1] == 'X')) {
            i += 2;
            for (; i < n; ++i) {
                const char c = p[i];
                hcpp::u32 d;
                if (c >= '0' && c <= '9') {
                    d = static_cast<hcpp::u32>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    d = static_cast<hcpp::u32>(c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                    d = static_cast<hcpp::u32>(c - 'A' + 10);
                } else {
                    break;
                }
                v = v * 16.0 + static_cast<double>(d);
                any = true;
            }
            ok = any;
            return neg ? -v : v;
        }
        for (; i < n && p[i] >= '0' && p[i] <= '9'; ++i) {
            v = v * 10.0 + static_cast<double>(p[i] - '0');
            any = true;
        }
        if (i < n && p[i] == '.') {
            ++i;
            double scale = 0.1;
            for (; i < n && p[i] >= '0' && p[i] <= '9'; ++i) {
                v += scale * static_cast<double>(p[i] - '0');
                scale *= 0.1;
                any = true;
            }
        }
        if (any && i < n && (p[i] == 'e' || p[i] == 'E')) {
            ++i;
            bool eneg = false;
            if (i < n && (p[i] == '+' || p[i] == '-')) {
                eneg = p[i] == '-';
                ++i;
            }
            hcpp::u32 e = 0;
            bool eany = false;
            for (; i < n && p[i] >= '0' && p[i] <= '9'; ++i) {
                e = e * 10 + static_cast<hcpp::u32>(p[i] - '0');
                eany = true;
            }
            if (eany) {
                double mult = 1.0;
                for (hcpp::u32 k = 0; k < e && k < 320; ++k) {
                    mult *= 10.0;
                }
                v = eneg ? v / mult : v * mult;
            }
        }
        // Trailing junk makes the whole parse NaN (JS: "12x" -> NaN).
        while (i < n && (p[i] == ' ' || p[i] == '\t')) {
            ++i;
        }
        ok = any && i >= n;
        if (!any) {
            // "   " -> 0 in JS ToNumber; only when fully whitespace.
            ok = i >= n;
            return 0.0;
        }
        return neg ? -v : v;
    }

    hcpp::i32 str_to_i32(const char* p, hcpp::u32 n) noexcept {
        bool ok = false;
        const double v = str_to_num(p, n, ok);
        if (!ok || num_is_nan(v) || num_is_nan(v - v)) {
            return 0;
        }
        hcpp::i64 i = 0;
        if (!num_is_int(d_trunc(v), i)) {
            return 0;
        }
        return static_cast<hcpp::i32>(i);
    }

    // ---- value -> string ------------------------------------------------------------------------

    const char* hjs_type_name(HjsValue v) noexcept {
        switch (v.type) {
            case HJS_UNDEFINED:
                return "undefined";
            case HJS_NULL:
                return "object";
            case HJS_BOOL:
                return "boolean";
            case HJS_NUMBER:
                return "number";
            case HJS_STRING:
                return "string";
            case HJS_FUNCTION:
            case HJS_NATIVE:
            case HJS_BOUND:
                return "function";
            default:
                return "object";
        }
    }

    double hjs_to_number(HjsValue v) noexcept { return hjs_to_num(v); }

    static void write_escaped_str(HjsBuf& b, const char* p,
                                  hcpp::u32 n) noexcept {
        for (hcpp::u32 i = 0; i < n; ++i) {
            const char c = p[i];
            switch (c) {
                case '\n':
                    b.s("\\n");
                    break;
                case '\r':
                    b.s("\\r");
                    break;
                case '\t':
                    b.s("\\t");
                    break;
                case '"':
                    b.s("\\\"");
                    break;
                case '\\':
                    b.s("\\\\");
                    break;
                default:
                    b.ch(c >= 32 && c < 127 ? c : '.');
            }
        }
    }

    // JS ToString() rendering (not JSON — no quotes around strings).
    void hjs_write_val(hjs_context& ctx, HjsBuf& buf, HjsValue v,
                       hcpp::u32 depth) noexcept {
        if (depth > hjs_json_depth_max) {
            buf.s("[...]");
            return;
        }
        switch (v.type) {
            case HJS_UNDEFINED:
                buf.s("undefined");
                break;
            case HJS_NULL:
                buf.s("null");
                break;
            case HJS_BOOL:
                buf.s(v.u.b ? "true" : "false");
                break;
            case HJS_NUMBER:
                buf.num(v.u.num);
                break;
            case HJS_STRING:
                if (v.u.str != nullptr) {
                    buf.sn(v.u.str->data, v.u.str->len);
                }
                break;
            case HJS_ARRAY: {
                const HjsArr* a = v.u.arr;
                for (hcpp::u32 i = 0; a != nullptr && i < a->len; ++i) {
                    if (i != 0) {
                        buf.ch(',');
                    }
                    if (a->items[i].type == HJS_UNDEFINED ||
                        a->items[i].type == HJS_NULL ||
                        a->items[i].type == HJS_FUNCTION) {
                        buf.s("null");
                    } else {
                        hjs_write_val(ctx, buf, a->items[i], depth + 1);
                    }
                }
                break;
            }
            case HJS_FUNCTION:
                buf.s("function ");
                buf.sn(v.u.fn->name, v.u.fn->name_len);
                buf.s("()");
                break;
            case HJS_NATIVE:
            case HJS_BOUND:
                buf.s("function () { [native code] }");
                break;
            case HJS_OBJECT:
            default:
                buf.s("[object Object]");
                break;
        }
    }

    HjsValue hjs_to_str(hjs_context& ctx, HjsValue v) noexcept {
        if (v.type == HJS_STRING) {
            return v;
        }
        char stack[384];
        HjsBuf b{stack, 0, sizeof(stack), false};
        hjs_write_val(ctx, b, v, 0);
        return hjs_str(ctx, stack, b.n);
    }

    hcpp::u32 hjs_to_string_buf(hjs_context* ctx, HjsValue v, char* out,
                                hcpp::u32 cap) noexcept {
        HjsBuf b{out, 0, cap, false};
        if (cap != 0) {
            out[0] = '\0';
        }
        hjs_write_val(*ctx, b, v, 0);
        return b.n;
    }

    // Escaped-quoted writer used by JSON.stringify.
    static void json_write_str(HjsBuf& b, const char* p,
                               hcpp::u32 n) noexcept {
        b.ch('"');
        write_escaped_str(b, p, n);
        b.ch('"');
    }

    bool json_stringify(hjs_context& ctx, HjsValue v, HjsBuf& buf,
                        hcpp::u32 depth) noexcept {
        if (depth > hjs_json_depth_max) {
            hjs_err(ctx, "TypeError", "circular / deep JSON");
            return false;
        }
        switch (v.type) {
            case HJS_UNDEFINED:
            case HJS_FUNCTION:
            case HJS_NATIVE:
            case HJS_BOUND:
                buf.s("null");
                return true;
            case HJS_NULL:
                buf.s("null");
                return true;
            case HJS_BOOL:
                buf.s(v.u.b ? "true" : "false");
                return true;
            case HJS_NUMBER:
                buf.num(v.u.num);
                return true;
            case HJS_STRING:
                json_write_str(buf, v.u.str->data, v.u.str->len);
                return true;
            case HJS_ARRAY: {
                buf.ch('[');
                const HjsArr* a = v.u.arr;
                for (hcpp::u32 i = 0; a != nullptr && i < a->len; ++i) {
                    if (i != 0) {
                        buf.ch(',');
                    }
                    if (!json_stringify(ctx, a->items[i], buf, depth + 1)) {
                        return false;
                    }
                }
                buf.ch(']');
                return true;
            }
            case HJS_OBJECT:
            default: {
                buf.ch('{');
                bool first = true;
                const HjsObj* o = v.u.obj;
                // Prop list is newest-first; count then walk to the
                // tail-most entries first so output preserves the
                // literal insertion order.
                hcpp::u32 total = 0;
                for (HjsProp* q = o != nullptr ? o->props : nullptr;
                     q != nullptr; q = q->next) {
                    ++total;
                }
                for (hcpp::u32 ord = 0; ord < total; ++ord) {
                    hcpp::u32 skip = total - 1 - ord;
                    HjsProp* p = o->props;
                    for (; p != nullptr && skip != 0; --skip) {
                        p = p->next;
                    }
                    if (p == nullptr) {
                        break;
                    }
                    HjsValue pv = p->v;
                    if (pv.type == HJS_UNDEFINED || pv.type == HJS_FUNCTION ||
                        pv.type == HJS_NATIVE || pv.type == HJS_BOUND) {
                        continue;
                    }
                    if (!first) {
                        buf.ch(',');
                    }
                    first = false;
                    json_write_str(buf, p->key, p->key_len);
                    buf.ch(':');
                    if (!json_stringify(ctx, pv, buf, depth + 1)) {
                        return false;
                    }
                }
                buf.ch('}');
                return true;
            }
        }
    }

}  // namespace hype::hjs
