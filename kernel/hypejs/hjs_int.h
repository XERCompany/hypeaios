#pragma once

// hypejs internals — shared between the lexer, parser, evaluator,
// builtins and the public API layer. Everything below is kernel-private;
// embedders see only hjs.h.

#include "hcpp/types.hpp"
#include "kernel/hypejs/hjs.h"

namespace hype::hjs {

    // ---- heap records ------------------------------------------------------------

    struct HjsStr {              // NUL-terminated, arena-owned
        hcpp::u32 len;
        char data[];
    };

    struct HjsProp {
        HjsProp* next;
        const char* key;         // arena string (or source slice interned)
        hcpp::u32 key_len;
        HjsValue v;
    };

    struct HjsObj {              // plain {k:v} property table
        HjsProp* props;
    };

    struct HjsArr {              // dense vector, arena chunks (grow-copy)
        HjsValue* items;
        hcpp::u32 len;
        hcpp::u32 cap;
    };

    struct HjsNode;              // AST node (below)
    struct HjsEnv;

    struct HjsFunc {
        const char* name;
        hcpp::u32 name_len;
        HjsNode** params;        // N_IDENT nodes
        hcpp::u32 param_count;
        HjsNode* body;           // N_BLOCK
        HjsEnv* env;             // defining environment (closure)
    };

    struct HjsNative {
        const char* name;
        HjsNativeFn fn;
    };

    struct HjsBound {            // arr.push / str.slice style bound methods
        HjsNativeFn fn;
        HjsValue self;
    };

    struct HjsBind {
        HjsBind* next;
        const char* name;
        hcpp::u32 name_len;
        HjsValue v;
        bool is_const;
    };

    struct HjsEnv {
        HjsEnv* parent;
        HjsBind* binds;
    };

    // ---- tokens -------------------------------------------------------------------

    enum HjsTK : hcpp::u8 {
        TK_EOF = 0, TK_NUM, TK_STR, TK_IDENT,
        TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
        TK_SEMI, TK_COMMA, TK_DOT, TK_COLON, TK_QUESTION,
        TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
        TK_AMP, TK_PIPE, TK_CARET, TK_TILDE, TK_BANG,
        TK_ASSIGN, TK_PLUS_EQ, TK_MINUS_EQ, TK_STAR_EQ, TK_SLASH_EQ,
        TK_PCT_EQ,
        TK_EQEQ, TK_NOTEQ, TK_EQEQEQ, TK_NOTEQEQ,
        TK_LT, TK_GT, TK_LE, TK_GE,
        TK_ANDAND, TK_OROR,
        TK_SHL, TK_SHR, TK_USHR,
        TK_INCR, TK_DECR,
        TK_ARROW,
        // keywords
        TK_VAR, TK_LET, TK_CONST, TK_FUNCTION, TK_RETURN, TK_IF, TK_ELSE,
        TK_WHILE, TK_DO, TK_FOR, TK_OF, TK_IN, TK_BREAK, TK_CONTINUE,
        TK_TRUE, TK_FALSE, TK_NULL, TK_UNDEFINED, TK_TYPEOF, TK_VOID,
        TK_DELETE, TK_THIS, TK_THROW, TK_TRY, TK_CATCH, TK_FINALLY,
        TK_NEW, TK_INSTANCEOF,
    };

    struct HjsTok {
        HjsTK kind;
        const char* p;           // slice into the source (or str_buf)
        hcpp::u32 n;
        double num;              // TK_NUM value
    };

    // Lexer cursor shared with the parser (kept out of the context so
    // hjs_eval can re-enter cleanly on later calls).
    struct HjsLex {
        hjs_context* ctx;
        const char* src;
        hcpp::u32 len;
        hcpp::u32 pos;
        // Decoded string scratch — escapes expand into an arena buffer
        // handed to the parser (reset per string token).
        char* str_buf;
        hcpp::u32 str_len;
        hcpp::u32 str_cap;
    };

    void lex_next(HjsLex& lx, HjsTok& t) noexcept;

    // ---- AST ------------------------------------------------------------------------

    enum HjsNK : hcpp::u8 {
        N_NUM = 0, N_STR, N_BOOL, N_NULL, N_UNDEF, N_IDENT, N_THIS,
        N_FUNC,                    // s=name?, items=params, a=body
        N_ARR, N_OBJ,              // items = elems / key-value pairs
        N_UNARY,                   // op=TK_*; a=operand
        N_POSTFIX,                 // op=TK_INCR/DECR; a=operand
        N_BIN,                     // op=TK_*; a,b (also &&/||)
        N_COND,                    // a ? b : c
        N_ASSIGN,                  // op=0 assign or TK_*_EQ; a=target,b=value
        N_MEMBER,                  // a=obj, s=prop name
        N_INDEX,                   // a=obj, b=index
        N_CALL,                    // a=callee, items=args; aux=1 -> `new`
        N_BLOCK,                   // items = stmts
        N_VARDECL,                 // op=TK_VAR/LET/CONST, s=name, a=init?
        N_EXPRSTMT,                // a=expr
        N_IF,                      // a=cond, b=then, c=else?
        N_WHILE,                   // a=cond, b=body
        N_DOWHILE,                 // a=cond, b=body
        N_FOR,                     // items[4] = init,test,update,body
        N_FOROF,                   // s=var name, op=decl kind/0, a=iter, b=body
        N_RETURN,                  // a=expr?
        N_BREAK, N_CONTINUE, N_EMPTY,
        N_THROW,                   // a=expr
        N_TRY,                     // a=try, s=catch param, b=catch?, c=finally?
    };

    // Fat AST node — one flat 64-byte record keeps arena accounting
    // simple and the parser free of per-kind allocation paths.
    struct HjsNode {
        HjsNK kind;
        hcpp::u8 op;
        hcpp::u8 aux;
        hcpp::u8 pad0;
        const char* s;
        hcpp::u32 sn;
        HjsNode* a;
        HjsNode* b;
        HjsNode* c;
        HjsNode** items;
        hcpp::u32 icount;
        double num;
    };

    // ---- context ----------------------------------------------------------------------

    enum HjsFlow : hcpp::u8 {
        F_NORMAL = 0,
        F_BREAK,
        F_CONTINUE,
        F_RETURN,
        F_THROW,                   // ctx.thrown carries the value
    };

    struct hjs_context {
        hcpp::u8* arena;
        hcpp::u32 arena_size;
        hcpp::u32 arena_used;

        HjsEnv* global_env;
        HjsObj* global_obj;        // `globalThis` value holder

        // Eval state.
        HjsFlow flow;
        HjsValue thrown;           // F_THROW payload / F_RETURN result
        HjsValue this_val;         // receiver of the active call frame
        hcpp::u64 steps;
        hcpp::u64 step_limit;
        hcpp::u32 call_depth;
        char error[96];            // first error text (also F_THROW str)

        // Static native records for builtins + registered natives.
        HjsNative natives[hjs_native_max];
        hcpp::u32 native_count;

        HjsOutputFn out;
        void* out_user;
        bool in_eval;
    };

    // ---- shared helpers ----------------------------------------------------------------

    void* hjs_alloc(hjs_context& ctx, hcpp::u32 bytes) noexcept;
    // Copies `n` chars into a fresh NUL-terminated arena string.
    char* hjs_str_copy(hjs_context& ctx, const char* p, hcpp::u32 n) noexcept;

    HjsValue hjs_str(hjs_context& ctx, const char* p,
                     hcpp::u32 n) noexcept;               // new HjsStr value
    HjsValue hjs_str(hjs_context& ctx, const char* p) noexcept;
    HjsValue hjs_obj(hjs_context& ctx) noexcept;
    HjsValue hjs_arr(hjs_context& ctx) noexcept;
    bool hjs_arr_push(hjs_context& ctx, HjsArr* a, HjsValue v) noexcept;
    HjsValue hjs_native_val(hjs_context& ctx, const char* name,
                            HjsNativeFn fn) noexcept;
    HjsValue hjs_bound_val(hjs_context& ctx, HjsNativeFn fn,
                           HjsValue self) noexcept;

    bool str_eq(const char* ap, hcpp::u32 an, const char* bp,
                hcpp::u32 bn) noexcept;
    bool str_eqz(const char* ap, hcpp::u32 an, const char* bz) noexcept;

    void hjs_err(hjs_context& ctx, const char* kind, const char* msg) noexcept;
    void hjs_err_at(hjs_context& ctx, const char* kind, const char* msg,
                    const char* name, hcpp::u32 name_len) noexcept;

    // Environment ops.
    HjsEnv* env_new(hjs_context& ctx, HjsEnv* parent) noexcept;
    bool env_define(hjs_context& ctx, HjsEnv* env, const char* name,
                    hcpp::u32 n, HjsValue v, bool is_const) noexcept;
    HjsBind* env_lookup(HjsEnv* env, const char* name,
                        hcpp::u32 n) noexcept;   // walks parents
    bool env_assign(hjs_context& ctx, HjsEnv* env, const char* name,
                    hcpp::u32 n, HjsValue v) noexcept;

    // Property ops.
    HjsProp* obj_find(HjsObj* o, const char* key, hcpp::u32 n) noexcept;
    bool obj_set(hjs_context& ctx, HjsObj* o, const char* key, hcpp::u32 n,
                 HjsValue v) noexcept;
    bool obj_del(HjsObj* o, const char* key, hcpp::u32 n) noexcept;

    // Conversions / compare.
    double hjs_to_num(HjsValue v) noexcept;
    bool hjs_truthy(HjsValue v) noexcept;
    bool hjs_strict_eq(HjsValue a, HjsValue b) noexcept;
    bool hjs_loose_eq(hjs_context& ctx, HjsValue a, HjsValue b) noexcept;
    bool num_is_nan(double x) noexcept;
    bool num_is_int(double x, hcpp::i64& out) noexcept;
    hcpp::u32 num_to_str(double v, char* out, hcpp::u32 cap) noexcept;
    double str_to_num(const char* p, hcpp::u32 n, bool& ok) noexcept;
    hcpp::i32 str_to_i32(const char* p, hcpp::u32 n) noexcept;
    // ToString into an arena HjsStr (concat-friendly).
    HjsValue hjs_to_str(hjs_context& ctx, HjsValue v) noexcept;
    // ToString into a growable scratch buffer (JSON / printing).
    struct HjsBuf;
    void hjs_write_val(hjs_context& ctx, HjsBuf& buf, HjsValue v,
                       hcpp::u32 depth) noexcept;

    // ---- lexer / parser / evaluator ------------------------------------------------------

    // Fills `*root` with the parsed program block; false -> ctx.error set.
    bool hjs_parse(hjs_context& ctx, const char* src, hcpp::u32 len,
                   HjsNode** root) noexcept;

    HjsValue eval_node(hjs_context& ctx, HjsEnv* env,
                       HjsNode* n) noexcept;
    HjsValue call_value(hjs_context& ctx, HjsValue fn, HjsValue self,
                        const HjsValue* args, hcpp::u32 argc) noexcept;

    // Member-name -> bound-native lookups (hjs_builtins.cc): give
    // `str.slice` / `arr.push` the receiver-bound method value.
    HjsNativeFn str_method(const char* name, hcpp::u32 n) noexcept;
    HjsNativeFn arr_method(const char* name, hcpp::u32 n) noexcept;

    // Installs print/console/Math/JSON/Array/global helpers + the kernel
    // native bridge (sys/fs/net/clip/notify) onto ctx.global_env.
    void hjs_install_builtins(hjs_context& ctx) noexcept;

    // JSON.stringify support shared with selftest.
    bool json_stringify(hjs_context& ctx, HjsValue v, HjsBuf& buf,
                        hcpp::u32 depth) noexcept;

    // Growable arena-free scratch buffer on the kernel stack for small
    // formatting tasks; big renders use HjsArenaBuf in hjs.cc.
    struct HjsBuf {
        char* d;
        hcpp::u32 n;
        hcpp::u32 cap;
        bool overflow;

        void ch(char c) noexcept {
            if (n + 1 >= cap) {
                overflow = true;
                return;
            }
            d[n++] = c;
            d[n] = '\0';
        }
        void s(const char* t) noexcept {
            while (t != nullptr && *t != '\0') {
                ch(*t++);
            }
        }
        void sn(const char* t, hcpp::u32 len) noexcept {
            for (hcpp::u32 i = 0; i < len; ++i) {
                ch(t[i]);
            }
        }
        void dec(hcpp::u64 v) noexcept {
            char tmp[20];
            hcpp::u32 m = 0;
            do {
                tmp[m++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            } while (v != 0 && m < sizeof(tmp));
            while (m != 0) {
                ch(tmp[--m]);
            }
        }
        void idec(hcpp::i64 v) noexcept {
            if (v < 0) {
                ch('-');
                dec(static_cast<hcpp::u64>(-(v + 1)) + 1);
                return;
            }
            dec(static_cast<hcpp::u64>(v));
        }
        void num(double v) noexcept {
            char tmp[32];
            const hcpp::u32 k = num_to_str(v, tmp, sizeof(tmp));
            sn(tmp, k);
        }
    };

}  // namespace hype::hjs
