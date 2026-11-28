// hypejs evaluator — tree-walking interpreter (issue #181).
//
// eval_node() dispatches on the AST node kind. Statements signal
// control flow through ctx.flow (F_BREAK/F_CONTINUE/F_RETURN/F_THROW);
// expressions return an HjsValue by copy. Every dispatch bumps the step
// counter — a script can burn at most ctx.step_limit node visits before
// the "script limit" error traps it. Call depth is separately capped.

#include "kernel/hypejs/hjs_int.h"

#include "hcpp/memory.hpp"

extern "C" double floor(double x) noexcept;
extern "C" double pow(double x, double y) noexcept;
extern "C" double fabs(double x) noexcept;

namespace hype::hjs {

    namespace {

        inline bool ok(const hjs_context& ctx) noexcept {
            return ctx.flow == F_NORMAL;
        }

        HjsValue undef() noexcept { return HjsValue::undefined(); }

        bool step(hjs_context& ctx) noexcept {
            if (++ctx.steps > ctx.step_limit) {
                hjs_err(ctx, "RangeError", "script step limit");
                return false;
            }
            return true;
        }

        // ---- property reads ------------------------------------------------------

        // str_method / arr_method are defined in hjs_builtins.cc and
        // declared in hjs_int.h — they map a property name to the bound
        // native implementing it.

        HjsValue get_prop(hjs_context& ctx, HjsValue base, const char* key,
                          hcpp::u32 kn) noexcept {
            switch (base.type) {
                case HJS_OBJECT: {
                    HjsProp* p = obj_find(base.u.obj, key, kn);
                    return p != nullptr ? p->v : undef();
                }
                case HJS_ARRAY: {
                    if (str_eqz(key, kn, "length")) {
                        return HjsValue::number(
                            static_cast<double>(base.u.arr->len));
                    }
                    if (HjsNativeFn fn = arr_method(key, kn);
                        fn != nullptr) {
                        return hjs_bound_val(ctx, fn, base);
                    }
                    return undef();
                }
                case HJS_STRING: {
                    if (str_eqz(key, kn, "length")) {
                        return HjsValue::number(
                            static_cast<double>(base.u.str->len));
                    }
                    if (HjsNativeFn fn = str_method(key, kn);
                        fn != nullptr) {
                        return hjs_bound_val(ctx, fn, base);
                    }
                    return undef();
                }
                case HJS_NULL:
                case HJS_UNDEFINED:
                    hjs_err_at(ctx, "TypeError",
                               ": cannot read property of null/undefined",
                               key, kn);
                    return undef();
                default:
                    return undef();  // numbers/bools/functions: no props
            }
        }

        bool arr_set(hjs_context& ctx, HjsArr* a, hcpp::u32 idx,
                     HjsValue v) noexcept {
            if (a == nullptr || idx >= 4096) {
                return false;
            }
            while (idx >= a->len) {
                if (!hjs_arr_push(ctx, a, undef())) {
                    return false;
                }
            }
            a->items[idx] = v;
            return true;
        }

        HjsValue get_index(hjs_context& ctx, HjsValue base,
                           HjsValue idx) noexcept {
            switch (base.type) {
                case HJS_ARRAY: {
                    if (idx.type == HJS_STRING) {
                        return get_prop(ctx, base, idx.u.str->data,
                                        idx.u.str->len);
                    }
                    hcpp::i64 i = 0;
                    if (idx.type != HJS_NUMBER ||
                        !num_is_int(idx.u.num, i) || i < 0 ||
                        i >= base.u.arr->len) {
                        return undef();
                    }
                    return base.u.arr->items[i];
                }
                case HJS_OBJECT: {
                    HjsValue key = hjs_to_str(ctx, idx);
                    if (key.type != HJS_STRING) {
                        return undef();
                    }
                    return get_prop(ctx, base, key.u.str->data,
                                    key.u.str->len);
                }
                case HJS_STRING: {
                    if (idx.type == HJS_STRING) {
                        return get_prop(ctx, base, idx.u.str->data,
                                        idx.u.str->len);
                    }
                    hcpp::i64 i = 0;
                    if (idx.type != HJS_NUMBER ||
                        !num_is_int(idx.u.num, i) || i < 0 ||
                        i >= base.u.str->len) {
                        return undef();
                    }
                    return hjs_str(ctx, base.u.str->data + i, 1);
                }
                case HJS_NULL:
                case HJS_UNDEFINED:
                    hjs_err(ctx, "TypeError",
                            "cannot index null/undefined");
                    return undef();
                default:
                    return undef();
            }
        }

        bool set_prop(hjs_context& ctx, HjsEnv* env, HjsNode* target,
                  HjsValue v) noexcept {
            if (target->kind == N_MEMBER) {
                HjsValue o = eval_node(ctx, env, target->a);
                if (!ok(ctx)) {
                    return false;
                }
                if (o.type == HJS_OBJECT) {
                    if (!obj_set(ctx, o.u.obj, target->s, target->sn, v)) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return false;
                    }
                    return true;
                }
                if (o.type == HJS_ARRAY && str_eqz(target->s, target->sn,
                                                 "length")) {
                    hcpp::i64 n = 0;
                    if (v.type == HJS_NUMBER && num_is_int(v.u.num, n) &&
                        n >= 0 && n <= 4096) {
                        HjsArr* a = o.u.arr;
                        while (a->len > static_cast<hcpp::u32>(n)) {
                            --a->len;
                        }
                        while (a->len < static_cast<hcpp::u32>(n)) {
                            if (!hjs_arr_push(ctx, a, undef())) {
                                hjs_err(ctx, "Error", "arena exhausted");
                                return false;
                            }
                        }
                        return true;
                    }
                    hjs_err(ctx, "RangeError", "bad array length");
                    return false;
                }
                hjs_err(ctx, "TypeError",
                        "cannot set property on non-object");
                return false;
            }
            if (target->kind == N_INDEX) {
                HjsValue o = eval_node(ctx, env, target->a);
                if (!ok(ctx)) {
                    return false;
                }
                HjsValue idx = eval_node(ctx, env, target->b);
                if (!ok(ctx)) {
                    return false;
                }
                if (o.type == HJS_ARRAY) {
                    hcpp::i64 i = 0;
                    if (idx.type == HJS_NUMBER &&
                        num_is_int(idx.u.num, i) && i >= 0) {
                        if (!arr_set(ctx, o.u.arr,
                                     static_cast<hcpp::u32>(i), v)) {
                            hjs_err(ctx, "RangeError", "array index cap");
                            return false;
                        }
                        return true;
                    }
                    // arr["k"] -> object-style named prop is not
                    // supported on arrays; report honestly.
                    hjs_err(ctx, "TypeError", "bad array index");
                    return false;
                }
                if (o.type == HJS_OBJECT) {
                    HjsValue key = hjs_to_str(ctx, idx);
                    if (key.type != HJS_STRING) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return false;
                    }
                    if (!obj_set(ctx, o.u.obj, key.u.str->data,
                                 key.u.str->len, v)) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return false;
                    }
                    return true;
                }
                hjs_err(ctx, "TypeError", "cannot index non-object");
                return false;
            }
            hjs_err(ctx, "ReferenceError", "bad assignment target");
            return false;
        }

        // Reads the current value of an assignable target node
        // (N_IDENT/N_MEMBER/N_INDEX). `base_out`/`idx_out` receive the
        // already-evaluated receiver and index so compound assignment
        // does not re-evaluate side-effecting subexpressions.
        HjsValue read_target(hjs_context& ctx, HjsEnv* env, HjsNode* t,
                         HjsValue* base_out, HjsValue* idx_out) noexcept {
            if (t->kind == N_IDENT) {
                HjsBind* b = env_lookup(env, t->s, t->sn);
                if (b == nullptr) {
                    hjs_err_at(ctx, "ReferenceError", " is not defined",
                               t->s, t->sn);
                    return undef();
                }
                return b->v;
            }
            HjsValue base = eval_node(ctx, env, t->a);
            if (!ok(ctx)) {
                return undef();
            }
            if (base_out != nullptr) {
                *base_out = base;
            }
            if (t->kind == N_MEMBER) {
                return get_prop(ctx, base, t->s, t->sn);
            }
            HjsValue idx = eval_node(ctx, env, t->b);
            if (!ok(ctx)) {
                return undef();
            }
            if (idx_out != nullptr) {
                *idx_out = idx;
            }
            return get_index(ctx, base, idx);
        }

        bool write_target(hjs_context& ctx, HjsEnv* env, HjsNode* t,
                          HjsValue base, HjsValue v,
                          const HjsValue* idx_in) noexcept {
            if (t->kind == N_IDENT) {
                return env_assign(ctx, env, t->s, t->sn, v);
            }
            if (t->kind == N_MEMBER) {
                if (base.type == HJS_OBJECT) {
                    if (!obj_set(ctx, base.u.obj, t->s, t->sn, v)) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return false;
                    }
                    return true;
                }
                hjs_err(ctx, "TypeError",
                        "cannot set property on non-object");
                return false;
            }
            // N_INDEX with base (and optionally index) already evaluated.
            HjsValue idx;
            if (idx_in != nullptr) {
                idx = *idx_in;
            } else {
                idx = eval_node(ctx, env, t->b);
                if (!ok(ctx)) {
                    return false;
                }
            }
            if (base.type == HJS_ARRAY) {
                hcpp::i64 i = 0;
                if (idx.type == HJS_NUMBER && num_is_int(idx.u.num, i) &&
                    i >= 0) {
                    if (!arr_set(ctx, base.u.arr, static_cast<hcpp::u32>(i),
                                 v)) {
                        hjs_err(ctx, "RangeError", "array index cap");
                        return false;
                    }
                    return true;
                }
                hjs_err(ctx, "TypeError", "bad array index");
                return false;
            }
            if (base.type == HJS_OBJECT) {
                HjsValue key = hjs_to_str(ctx, idx);
                if (key.type != HJS_STRING ||
                    !obj_set(ctx, base.u.obj, key.u.str->data,
                             key.u.str->len, v)) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return false;
                }
                return true;
            }
            hjs_err(ctx, "TypeError", "cannot index non-object");
            return false;
        }

        // ---- operators ---------------------------------------------------------------

        hcpp::i32 to_i32(double v) noexcept {
            const double d = v - v;  // NaN for +-inf
            if (v == 0.0 || num_is_nan(v) || num_is_nan(d)) {
                return 0;
            }
            // Reduce modulo 2^32 like JS ToInt32.
            double t = v - floor(v / 4294967296.0) * 4294967296.0;
            if (t >= 2147483648.0) {
                t -= 4294967296.0;
            }
            return static_cast<hcpp::i32>(static_cast<hcpp::i64>(t));
        }

        bool str_less(const char* ap, hcpp::u32 an, const char* bp,
                      hcpp::u32 bn) noexcept {
            const hcpp::u32 m = an < bn ? an : bn;
            for (hcpp::u32 i = 0; i < m; ++i) {
                if (ap[i] != bp[i]) {
                    return ap[i] < bp[i];
                }
            }
            return an < bn;
        }

        bool is_stringy(HjsValue v) noexcept {
            return v.type == HJS_STRING || v.type == HJS_OBJECT ||
                   v.type == HJS_ARRAY;
        }

        HjsValue bin_op(hjs_context& ctx, HjsEnv* env, HjsNode* n) noexcept {
            const HjsTK op = static_cast<HjsTK>(n->op);
            // Short-circuit logicals keep operand values like real JS.
            if (op == TK_ANDAND) {
                HjsValue l = eval_node(ctx, env, n->a);
                if (!ok(ctx) || !hjs_truthy(l)) {
                    return l;
                }
                return eval_node(ctx, env, n->b);
            }
            if (op == TK_OROR) {
                HjsValue l = eval_node(ctx, env, n->a);
                if (!ok(ctx) || hjs_truthy(l)) {
                    return l;
                }
                return eval_node(ctx, env, n->b);
            }
            HjsValue l = eval_node(ctx, env, n->a);
            if (!ok(ctx)) {
                return undef();
            }
            HjsValue r = eval_node(ctx, env, n->b);
            if (!ok(ctx)) {
                return undef();
            }
            switch (op) {
                case TK_PLUS:
                    if (is_stringy(l) || is_stringy(r)) {
                        HjsValue ls = hjs_to_str(ctx, l);
                        HjsValue rs = hjs_to_str(ctx, r);
                        if (ls.type != HJS_STRING || rs.type != HJS_STRING) {
                            hjs_err(ctx, "Error", "arena exhausted");
                            return undef();
                        }
                        const hcpp::u32 total =
                            ls.u.str->len + rs.u.str->len;
                        char* buf = static_cast<char*>(
                            hjs_alloc(ctx, total + 1));
                        if (buf == nullptr) {
                            hjs_err(ctx, "Error", "arena exhausted");
                            return undef();
                        }
                        hcpp::copy(buf, ls.u.str->data, ls.u.str->len);
                        hcpp::copy(buf + ls.u.str->len, rs.u.str->data,
                                   rs.u.str->len);
                        return hjs_str(ctx, buf, total);
                    }
                    return HjsValue::number(hjs_to_num(l) + hjs_to_num(r));
                case TK_MINUS:
                    return HjsValue::number(hjs_to_num(l) - hjs_to_num(r));
                case TK_STAR:
                    return HjsValue::number(hjs_to_num(l) * hjs_to_num(r));
                case TK_SLASH:
                    return HjsValue::number(hjs_to_num(l) / hjs_to_num(r));
                case TK_PERCENT: {
                    const double a = hjs_to_num(l);
                    const double b = hjs_to_num(r);
                    // a - a is NaN only when a is +-inf.
                    if (b == 0.0 || num_is_nan(a) || num_is_nan(b) ||
                        num_is_nan(a - a)) {
                        return HjsValue::number(__builtin_nan(""));
                    }
                    const double q = a / b;
                    const double qt =
                        q < 0 ? -floor(-q) : floor(q);  // trunc
                    return HjsValue::number(a - qt * b);
                }
                case TK_EQEQ:
                    return HjsValue::boolean(hjs_loose_eq(ctx, l, r));
                case TK_NOTEQ:
                    return HjsValue::boolean(!hjs_loose_eq(ctx, l, r));
                case TK_EQEQEQ:
                    return HjsValue::boolean(hjs_strict_eq(l, r));
                case TK_NOTEQEQ:
                    return HjsValue::boolean(!hjs_strict_eq(l, r));
                case TK_LT:
                case TK_GT:
                case TK_LE:
                case TK_GE: {
                    if (l.type == HJS_STRING && r.type == HJS_STRING) {
                        const bool less =
                            str_less(l.u.str->data, l.u.str->len,
                                     r.u.str->data, r.u.str->len);
                        const bool greater =
                            str_less(r.u.str->data, r.u.str->len,
                                     l.u.str->data, l.u.str->len);
                        switch (op) {
                            case TK_LT:
                                return HjsValue::boolean(less);
                            case TK_GT:
                                return HjsValue::boolean(greater);
                            case TK_LE:
                                return HjsValue::boolean(!greater);
                            default:
                                return HjsValue::boolean(!less);
                        }
                    }
                    const double a = hjs_to_num(l);
                    const double b = hjs_to_num(r);
                    bool res = false;
                    switch (op) {
                        case TK_LT:
                            res = a < b;
                            break;
                        case TK_GT:
                            res = a > b;
                            break;
                        case TK_LE:
                            res = a <= b;
                            break;
                        default:
                            res = a >= b;
                            break;
                    }
                    return HjsValue::boolean(res);
                }
                case TK_AMP:
                    return HjsValue::number(static_cast<double>(
                        to_i32(hjs_to_num(l)) & to_i32(hjs_to_num(r))));
                case TK_PIPE:
                    return HjsValue::number(static_cast<double>(
                        to_i32(hjs_to_num(l)) | to_i32(hjs_to_num(r))));
                case TK_CARET:
                    return HjsValue::number(static_cast<double>(
                        to_i32(hjs_to_num(l)) ^ to_i32(hjs_to_num(r))));
                case TK_SHL:
                    return HjsValue::number(static_cast<double>(
                        to_i32(hjs_to_num(l))
                        << (to_i32(hjs_to_num(r)) & 31)));
                case TK_SHR:
                    return HjsValue::number(static_cast<double>(
                        to_i32(hjs_to_num(l))
                        >> (to_i32(hjs_to_num(r)) & 31)));
                case TK_USHR:
                    return HjsValue::number(static_cast<double>(
                        static_cast<hcpp::u32>(to_i32(hjs_to_num(l)))
                        >> (to_i32(hjs_to_num(r)) & 31)));
                case TK_IN: {
                    HjsValue key = hjs_to_str(ctx, l);
                    if (key.type != HJS_STRING) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return undef();
                    }
                    if (r.type == HJS_OBJECT) {
                        return HjsValue::boolean(
                            obj_find(r.u.obj, key.u.str->data,
                                     key.u.str->len) != nullptr);
                    }
                    if (r.type == HJS_ARRAY) {
                        hcpp::i64 i = 0;
                        return HjsValue::boolean(
                            l.type == HJS_NUMBER &&
                            num_is_int(l.u.num, i) && i >= 0 &&
                            i < r.u.arr->len);
                    }
                    hjs_err(ctx, "TypeError", "'in' needs object right side");
                    return undef();
                }
                case TK_INSTANCEOF:
                    hjs_err(ctx, "TypeError",
                            "instanceof unsupported (no prototypes)");
                    return undef();
                default:
                    hjs_err(ctx, "Error", "bad binary op");
                    return undef();
            }
        }

        HjsValue unary_op(hjs_context& ctx, HjsEnv* env,
                          HjsNode* n) noexcept {
            const HjsTK op = static_cast<HjsTK>(n->op);
            if (op == TK_TYPEOF) {
                // typeof an unbound name must not raise.
                if (n->a->kind == N_IDENT &&
                    env_lookup(env, n->a->s, n->a->sn) == nullptr) {
                    return hjs_str(ctx, "undefined", 9);
                }
                HjsValue v = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                const char* name = hjs_type_name(v);
                hcpp::u32 len = 0;
                while (name[len] != '\0') {
                    ++len;
                }
                return hjs_str(ctx, name, len);
            }
            if (op == TK_VOID) {
                (void)eval_node(ctx, env, n->a);
                return undef();
            }
            if (op == TK_DELETE) {
                if (n->a->kind == N_MEMBER) {
                    HjsValue base = eval_node(ctx, env, n->a->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    return HjsValue::boolean(
                        base.type == HJS_OBJECT &&
                        obj_del(base.u.obj, n->a->s, n->a->sn));
                }
                if (n->a->kind == N_INDEX) {
                    HjsValue base = eval_node(ctx, env, n->a->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    HjsValue idx = eval_node(ctx, env, n->a->b);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    if (base.type == HJS_OBJECT) {
                        HjsValue key = hjs_to_str(ctx, idx);
                        return HjsValue::boolean(
                            key.type == HJS_STRING &&
                            obj_del(base.u.obj, key.u.str->data,
                                    key.u.str->len));
                    }
                    if (base.type == HJS_ARRAY) {
                        hcpp::i64 i = 0;
                        if (idx.type == HJS_NUMBER &&
                            num_is_int(idx.u.num, i) && i >= 0 &&
                            i < base.u.arr->len) {
                            base.u.arr->items[i] = undef();
                        }
                        return HjsValue::boolean(true);
                    }
                    return HjsValue::boolean(false);
                }
                (void)eval_node(ctx, env, n->a);
                return HjsValue::boolean(true);
            }
            if (op == TK_INCR || op == TK_DECR) {
                // Prefix ++/--: read, adjust, write, return new value.
                HjsValue base{};
                HjsValue idx{};
                HjsValue cur = read_target(ctx, env, n->a, &base, &idx);
                if (!ok(ctx)) {
                    return undef();
                }
                const double nv =
                    hjs_to_num(cur) + (op == TK_INCR ? 1.0 : -1.0);
                const HjsValue v = HjsValue::number(nv);
                if (n->a->kind == N_IDENT) {
                    if (!env_assign(ctx, env, n->a->s, n->a->sn, v)) {
                        return undef();
                    }
                } else if (!write_target(ctx, env, n->a, base, v, &idx)) {
                    return undef();
                }
                return v;
            }
            HjsValue v = eval_node(ctx, env, n->a);
            if (!ok(ctx)) {
                return undef();
            }
            switch (op) {
                case TK_BANG:
                    return HjsValue::boolean(!hjs_truthy(v));
                case TK_MINUS:
                    return HjsValue::number(-hjs_to_num(v));
                case TK_PLUS:
                    return HjsValue::number(hjs_to_num(v));
                case TK_TILDE:
                    return HjsValue::number(static_cast<double>(
                        ~to_i32(hjs_to_num(v))));
                default:
                    hjs_err(ctx, "Error", "bad unary op");
                    return undef();
            }
        }

        // ---- calls --------------------------------------------------------------------

        void eval_args(hjs_context& ctx, HjsEnv* env, HjsNode* call,
                       HjsValue* out, hcpp::u32 cap,
                       hcpp::u32* argc_out) noexcept {
            *argc_out = 0;
            for (hcpp::u32 i = 0; i < call->icount; ++i) {
                HjsValue a = eval_node(ctx, env, call->items[i]);
                if (!ok(ctx)) {
                    return;
                }
                if (i < cap) {
                    out[i] = a;
                    *argc_out = i + 1;
                }
            }
        }

    }  // namespace

    HjsValue call_value(hjs_context& ctx, HjsValue fn, HjsValue self,
                        const HjsValue* args, hcpp::u32 argc) noexcept {
        if (fn.type == HJS_NATIVE) {
            if (fn.u.nat == nullptr || fn.u.nat->fn == nullptr) {
                hjs_err(ctx, "TypeError", "not a function");
                return undef();
            }
            return fn.u.nat->fn(ctx, self, args, argc);
        }
        if (fn.type == HJS_BOUND) {
            if (fn.u.bound == nullptr) {
                hjs_err(ctx, "TypeError", "not a function");
                return undef();
            }
            return fn.u.bound->fn(ctx, fn.u.bound->self, args, argc);
        }
        if (fn.type != HJS_FUNCTION || fn.u.fn == nullptr) {
            hjs_err(ctx, "TypeError", "not a function");
            return undef();
        }
        if (ctx.call_depth >= hjs_call_depth_max) {
            hjs_err(ctx, "RangeError", "call depth limit");
            return undef();
        }
        ++ctx.call_depth;
        HjsFunc* f = fn.u.fn;
        HjsEnv* call_env = env_new(ctx, f->env);
        if (call_env == nullptr) {
            --ctx.call_depth;
            hjs_err(ctx, "Error", "arena exhausted");
            return undef();
        }
        for (hcpp::u32 i = 0; i < f->param_count; ++i) {
            const HjsNode* param = f->params[i];
            env_define(ctx, call_env, param->s, param->sn,
                       i < argc ? args[i] : undef(), false);
        }
        const HjsValue saved_this = ctx.this_val;
        ctx.this_val = self;
        const HjsValue ret = eval_node(ctx, call_env, f->body);
        ctx.this_val = saved_this;
        --ctx.call_depth;
        if (ctx.flow == F_RETURN) {
            ctx.flow = F_NORMAL;
            return ctx.thrown;
        }
        if (ctx.flow == F_THROW) {
            return undef();
        }
        // F_BREAK/F_CONTINUE leaking out of a function is a script bug;
        // surface it rather than looping forever.
        if (ctx.flow != F_NORMAL) {
            ctx.flow = F_NORMAL;
            hjs_err(ctx, "SyntaxError", "break/continue outside loop");
            return undef();
        }
        return ret;
    }

    HjsValue eval_node(hjs_context& ctx, HjsEnv* env, HjsNode* n) noexcept {
        if (n == nullptr || !ok(ctx)) {
            return undef();
        }
        if (!step(ctx)) {
            return undef();
        }
        switch (n->kind) {
            case N_NUM:
                return HjsValue::number(n->num);
            case N_STR:
                return hjs_str(ctx, n->s, n->sn);
            case N_BOOL:
                return HjsValue::boolean(n->num != 0.0);
            case N_NULL:
                return HjsValue::null();
            case N_UNDEF:
                return undef();
            case N_THIS:
                return ctx.this_val;
            case N_IDENT: {
                HjsBind* b = env_lookup(env, n->s, n->sn);
                if (b == nullptr) {
                    hjs_err_at(ctx, "ReferenceError", " is not defined",
                               n->s, n->sn);
                    return undef();
                }
                return b->v;
            }
            case N_FUNC: {
                auto* f = static_cast<HjsFunc*>(
                    hjs_alloc(ctx, sizeof(HjsFunc)));
                if (f == nullptr) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return undef();
                }
                f->name = n->s;
                f->name_len = n->sn;
                f->params = n->items;
                f->param_count = n->icount;
                f->body = n->a;
                f->env = env;
                HjsValue v{};
                v.type = HJS_FUNCTION;
                v.u.fn = f;
                return v;
            }
            case N_ARR: {
                HjsValue arr = hjs_arr(ctx);
                if (arr.type != HJS_ARRAY) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return undef();
                }
                for (hcpp::u32 i = 0; i < n->icount; ++i) {
                    HjsValue e = eval_node(ctx, env, n->items[i]);
                    if (!ok(ctx) ||
                        !hjs_arr_push(ctx, arr.u.arr, e)) {
                        if (ok(ctx)) {
                            hjs_err(ctx, "Error", "arena exhausted");
                        }
                        return undef();
                    }
                }
                return arr;
            }
            case N_OBJ: {
                HjsValue o = hjs_obj(ctx);
                if (o.type != HJS_OBJECT) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return undef();
                }
                for (hcpp::u32 i = 0; i + 1 < n->icount; i += 2) {
                    const HjsNode* key = n->items[i];
                    HjsValue v = eval_node(ctx, env, n->items[i + 1]);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    if (key == nullptr || key->kind != N_STR ||
                        !obj_set(ctx, o.u.obj, key->s, key->sn, v)) {
                        hjs_err(ctx, "Error", "bad object literal");
                        return undef();
                    }
                }
                return o;
            }
            case N_MEMBER: {
                HjsValue base = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                return get_prop(ctx, base, n->s, n->sn);
            }
            case N_INDEX: {
                HjsValue base = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                HjsValue idx = eval_node(ctx, env, n->b);
                if (!ok(ctx)) {
                    return undef();
                }
                return get_index(ctx, base, idx);
            }
            case N_BIN:
                return bin_op(ctx, env, n);
            case N_COND: {
                HjsValue c = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                return eval_node(ctx, env,
                                 hjs_truthy(c) ? n->b : n->c);
            }
            case N_UNARY:
                return unary_op(ctx, env, n);
            case N_POSTFIX: {
                HjsValue base{};
                HjsValue idx{};
                HjsValue cur = read_target(ctx, env, n->a, &base, &idx);
                if (!ok(ctx)) {
                    return undef();
                }
                const double nv =
                    hjs_to_num(cur) +
                    (static_cast<HjsTK>(n->op) == TK_INCR ? 1.0 : -1.0);
                const HjsValue v = HjsValue::number(nv);
                if (n->a->kind == N_IDENT) {
                    if (!env_assign(ctx, env, n->a->s, n->a->sn, v)) {
                        return undef();
                    }
                } else if (!write_target(ctx, env, n->a, base, v, &idx)) {
                    return undef();
                }
                return cur;  // postfix yields the OLD value
            }
            case N_ASSIGN: {
                HjsValue rhs = eval_node(ctx, env, n->b);
                if (!ok(ctx)) {
                    return undef();
                }
                if (static_cast<HjsTK>(n->op) != TK_ASSIGN) {
                    HjsValue base{};
                    HjsValue idx{};
                    HjsValue cur = read_target(ctx, env, n->a, &base, &idx);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    const HjsTK eq =
                        static_cast<HjsTK>(n->op);
                    double nv = 0.0;
                    bool is_num = true;
                    switch (eq) {
                        case TK_PLUS_EQ:
                            if (is_stringy(cur) || is_stringy(rhs)) {
                                HjsValue ls = hjs_to_str(ctx, cur);
                                HjsValue rs = hjs_to_str(ctx, rhs);
                                if (ls.type != HJS_STRING ||
                                    rs.type != HJS_STRING) {
                                    hjs_err(ctx, "Error",
                                            "arena exhausted");
                                    return undef();
                                }
                                const hcpp::u32 total = ls.u.str->len +
                                                        rs.u.str->len;
                                char* buf = static_cast<char*>(
                                    hjs_alloc(ctx, total + 1));
                                if (buf == nullptr) {
                                    hjs_err(ctx, "Error",
                                            "arena exhausted");
                                    return undef();
                                }
                                hcpp::copy(buf, ls.u.str->data,
                                           ls.u.str->len);
                                hcpp::copy(buf + ls.u.str->len,
                                           rs.u.str->data, rs.u.str->len);
                                rhs = hjs_str(ctx, buf, total);
                                is_num = false;
                                break;
                            }
                            nv = hjs_to_num(cur) + hjs_to_num(rhs);
                            break;
                        case TK_MINUS_EQ:
                            nv = hjs_to_num(cur) - hjs_to_num(rhs);
                            break;
                        case TK_STAR_EQ:
                            nv = hjs_to_num(cur) * hjs_to_num(rhs);
                            break;
                        case TK_SLASH_EQ:
                            nv = hjs_to_num(cur) / hjs_to_num(rhs);
                            break;
                        case TK_PCT_EQ: {
                            const double a = hjs_to_num(cur);
                            const double b = hjs_to_num(rhs);
                            const double q = a / b;
                            const double qt =
                                q < 0 ? -floor(-q) : floor(q);
                            nv = a - qt * b;
                            break;
                        }
                        default:
                            nv = 0.0;
                            break;
                    }
                    if (is_num) {
                        rhs = HjsValue::number(nv);
                    }
                    if (n->a->kind == N_IDENT) {
                        if (!env_assign(ctx, env, n->a->s, n->a->sn,
                                        rhs)) {
                            return undef();
                        }
                        return rhs;
                    }
                    if (!write_target(ctx, env, n->a, base, rhs, &idx)) {
                        return undef();
                    }
                    return rhs;
                }
                if (n->a->kind == N_IDENT) {
                    if (!env_assign(ctx, env, n->a->s, n->a->sn, rhs)) {
                        return undef();
                    }
                    return rhs;
                }
                if (!set_prop(ctx, env, n->a, rhs)) {
                    return undef();
                }
                return rhs;
            }
            case N_CALL: {
                // Resolve the callee once, keeping the receiver for
                // method calls (obj.m() binds this=obj).
                HjsValue fn;
                HjsValue self = ctx.this_val;
                bool has_self = false;
                if (n->a->kind == N_MEMBER) {
                    HjsValue base = eval_node(ctx, env, n->a->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    self = base;
                    has_self = true;
                    fn = get_prop(ctx, base, n->a->s, n->a->sn);
                } else if (n->a->kind == N_INDEX) {
                    HjsValue base = eval_node(ctx, env, n->a->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    self = base;
                    has_self = true;
                    HjsValue idx = eval_node(ctx, env, n->a->b);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    fn = get_index(ctx, base, idx);
                } else {
                    fn = eval_node(ctx, env, n->a);
                    has_self = false;
                }
                if (!ok(ctx)) {
                    return undef();
                }
                constexpr hcpp::u32 k_arg_cap = 16;
                HjsValue argv[k_arg_cap];
                hcpp::u32 argc = 0;
                eval_args(ctx, env, n, argv, k_arg_cap, &argc);
                if (!ok(ctx)) {
                    return undef();
                }
                if (n->aux != 0) {
                    // `new F(...)`: fresh object becomes `this`; an
                    // object return wins over it (constructor pattern).
                    HjsValue fresh = hjs_obj(ctx);
                    if (fresh.type != HJS_OBJECT) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return undef();
                    }
                    HjsValue r =
                        call_value(ctx, fn, fresh, argv, argc);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    return r.type == HJS_OBJECT || r.type == HJS_ARRAY ||
                                   r.type == HJS_FUNCTION
                               ? r
                               : fresh;
                }
                return call_value(
                    ctx, fn,
                    has_self ? self : HjsValue::undefined(), argv, argc);
            }
            // ---- statements ----------------------------------------------------------
            case N_EMPTY:
                return undef();
            case N_EXPRSTMT:
                return eval_node(ctx, env, n->a);
            case N_BLOCK: {
                HjsValue last = undef();
                for (hcpp::u32 i = 0; ok(ctx) && i < n->icount; ++i) {
                    last = eval_node(ctx, env, n->items[i]);
                }
                return last;
            }
            case N_VARDECL: {
                HjsValue v = undef();
                if (n->a != nullptr) {
                    v = eval_node(ctx, env, n->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                }
                if (!env_define(ctx, env, n->s, n->sn, v,
                                static_cast<HjsTK>(n->op) == TK_CONST)) {
                    hjs_err(ctx, "Error", "arena exhausted");
                    return undef();
                }
                return v;
            }
            case N_IF: {
                HjsValue c = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                if (hjs_truthy(c)) {
                    return eval_node(ctx, env, n->b);
                }
                if (n->c != nullptr) {
                    return eval_node(ctx, env, n->c);
                }
                return undef();
            }
            case N_WHILE: {
                while (ok(ctx)) {
                    HjsValue c = eval_node(ctx, env, n->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    if (!hjs_truthy(c)) {
                        break;
                    }
                    (void)eval_node(ctx, env, n->b);
                    if (ctx.flow == F_BREAK) {
                        ctx.flow = F_NORMAL;
                        break;
                    }
                    if (ctx.flow == F_CONTINUE) {
                        ctx.flow = F_NORMAL;
                    }
                }
                return undef();
            }
            case N_DOWHILE: {
                do {
                    (void)eval_node(ctx, env, n->b);
                    if (ctx.flow == F_BREAK) {
                        ctx.flow = F_NORMAL;
                        break;
                    }
                    if (ctx.flow == F_CONTINUE) {
                        ctx.flow = F_NORMAL;
                    }
                    if (!ok(ctx)) {
                        return undef();
                    }
                    HjsValue c = eval_node(ctx, env, n->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                    if (!hjs_truthy(c)) {
                        break;
                    }
                } while (true);
                return undef();
            }
            case N_FOR: {
                if (n->items == nullptr || n->icount != 4) {
                    hjs_err(ctx, "Error", "bad for node");
                    return undef();
                }
                HjsNode* init = n->items[0];
                HjsNode* test = n->items[1];
                HjsNode* update = n->items[2];
                HjsNode* body = n->items[3];
                if (init != nullptr && init->kind != N_EMPTY) {
                    (void)eval_node(ctx, env, init);
                    if (!ok(ctx)) {
                        return undef();
                    }
                }
                while (ok(ctx)) {
                    if (test != nullptr && test->kind != N_EMPTY) {
                        HjsValue c = eval_node(ctx, env, test);
                        if (!ok(ctx)) {
                            return undef();
                        }
                        if (!hjs_truthy(c)) {
                            break;
                        }
                    }
                    (void)eval_node(ctx, env, body);
                    if (ctx.flow == F_BREAK) {
                        ctx.flow = F_NORMAL;
                        break;
                    }
                    if (ctx.flow == F_CONTINUE) {
                        ctx.flow = F_NORMAL;
                    }
                    if (!ok(ctx)) {
                        return undef();
                    }
                    if (update != nullptr && update->kind != N_EMPTY) {
                        (void)eval_node(ctx, env, update);
                        if (!ok(ctx)) {
                            return undef();
                        }
                    }
                }
                return undef();
            }
            case N_FOROF: {
                HjsValue it = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                const bool declare = n->op != 0;
                hcpp::u32 count = 0;
                for (;;) {
                    HjsValue elem = undef();
                    bool more = false;
                    if (it.type == HJS_ARRAY) {
                        more = count < it.u.arr->len;
                        if (more) {
                            elem = it.u.arr->items[count];
                        }
                    } else if (it.type == HJS_STRING) {
                        more = count < it.u.str->len;
                        if (more) {
                            elem = hjs_str(ctx, it.u.str->data + count, 1);
                        }
                    } else if (it.type == HJS_OBJECT) {
                        // Extension: for-of over an object yields the
                        // property names (for-in style), oldest first.
                        HjsProp* p = it.u.obj->props;
                        hcpp::u32 total = 0;
                        for (HjsProp* q = p; q != nullptr; q = q->next) {
                            ++total;
                        }
                        more = count < total;
                        if (more) {
                            hcpp::u32 idx = total - 1 - count;
                            for (; p != nullptr && idx != 0; --idx) {
                                p = p->next;
                            }
                            if (p != nullptr) {
                                elem = hjs_str(ctx, p->key, p->key_len);
                            }
                        }
                    } else {
                        hjs_err(ctx, "TypeError",
                                "value is not iterable");
                        return undef();
                    }
                    if (!more) {
                        break;
                    }
                    if (declare) {
                        if (!env_define(ctx, env, n->s, n->sn, elem,
                                        static_cast<HjsTK>(n->op) ==
                                            TK_CONST)) {
                            hjs_err(ctx, "Error", "arena exhausted");
                            return undef();
                        }
                    } else if (!env_assign(ctx, env, n->s, n->sn, elem)) {
                        return undef();
                    }
                    (void)eval_node(ctx, env, n->b);
                    if (ctx.flow == F_BREAK) {
                        ctx.flow = F_NORMAL;
                        break;
                    }
                    if (ctx.flow == F_CONTINUE) {
                        ctx.flow = F_NORMAL;
                    }
                    if (!ok(ctx)) {
                        return undef();
                    }
                    ++count;
                }
                return undef();
            }
            case N_RETURN:
                if (n->a != nullptr) {
                    ctx.thrown = eval_node(ctx, env, n->a);
                    if (!ok(ctx)) {
                        return undef();
                    }
                } else {
                    ctx.thrown = undef();
                }
                ctx.flow = F_RETURN;
                return undef();
            case N_BREAK:
                ctx.flow = F_BREAK;
                return undef();
            case N_CONTINUE:
                ctx.flow = F_CONTINUE;
                return undef();
            case N_THROW: {
                HjsValue v = eval_node(ctx, env, n->a);
                if (!ok(ctx)) {
                    return undef();
                }
                ctx.thrown = v;
                ctx.flow = F_THROW;
                if (ctx.error[0] == '\0') {
                    char tmp[80];
                    HjsBuf b{tmp, 0, sizeof(tmp), false};
                    b.s("uncaught: ");
                    hjs_write_val(ctx, b, v, 0);
                    tmp[b.n] = '\0';
                    hcpp::u32 i = 0;
                    for (; i + 1 < sizeof(ctx.error) && tmp[i] != '\0';
                         ++i) {
                        ctx.error[i] = tmp[i];
                    }
                    ctx.error[i] = '\0';
                }
                return undef();
            }
            case N_TRY: {
                (void)eval_node(ctx, env, n->a);
                HjsValue thrown = ctx.thrown;
                if (ctx.flow == F_THROW && n->b != nullptr) {
                    // Catch: bind the thrown value to a fresh env.
                    ctx.flow = F_NORMAL;
                    ctx.thrown = undef();
                    HjsEnv* catch_env = env_new(ctx, env);
                    if (catch_env == nullptr) {
                        hjs_err(ctx, "Error", "arena exhausted");
                        return undef();
                    }
                    if (n->s != nullptr && n->sn != 0) {
                        env_define(ctx, catch_env, n->s, n->sn, thrown,
                                   false);
                    }
                    // The error text belongs to the throw; clear it so
                    // later errors can record afresh.
                    ctx.error[0] = '\0';
                    (void)eval_node(ctx, catch_env, n->b);
                }
                if (n->c != nullptr) {
                    const HjsFlow saved = ctx.flow;
                    const HjsValue saved_thrown = ctx.thrown;
                    ctx.flow = F_NORMAL;
                    (void)eval_node(ctx, env, n->c);
                    if (ctx.flow == F_NORMAL) {
                        ctx.flow = saved;
                        ctx.thrown = saved_thrown;
                    }
                }
                return undef();
            }
            default:
                hjs_err(ctx, "Error", "bad AST node");
                return undef();
        }
    }

}  // namespace hype::hjs
