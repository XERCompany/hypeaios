// hypejs parser — recursive-descent AST builder (issue #181).
//
// Grammar (ES-subset):
//   program    := stmt* EOF
//   stmt       := block | varstmt | if | while | do | for | return |
//                 break | continue | throw | try | funcdecl | expr ';'
//   expr       := assign
//   assign     := cond (('='|'+='|'-='|'*='|'/='|'%=') assign)?
//   cond       := or ('?' assign ':' assign)?
//   or         := and ('||' and)*
//   and        := bitor ('&&' bitor)*
//   bitor      := bitxor ('|' bitxor)*
//   bitxor     := bitand ('^' bitand)*
//   bitand     := equality ('&' equality)*
//   equality   := relational (('=='|'!='|'==='|'!==') relational)*
//   relational := shift (('<'|'>'|'<='|'>='|'in'|'instanceof') shift)*
//   shift      := additive (('<<'|'>>'|'>>>') additive)*
//   additive   := mult (('+'|'-') mult)*
//   mult       := unary (('*'|'/'|'%') unary)*
//   unary      := ('!'|'~'|'-'|'+'|typeof|void|delete|'++'|'--'|new) unary
//   postfix    := member ('++'|'--')?
//   member     := primary ('.'ident|'['expr']'|'('args')')*
//   primary    := literal | ident | '(' expr ')' | '[' elems ']'
//                 | '{' props '}' | 'function' name? '(' params ')' block
//                 | 'this' | arrow-fn
//
// Semicolons are optional before '}', EOF and generally tolerated —
// the engine does not track line numbers so full ASI is out of scope.

#include "kernel/hypejs/hjs_int.h"

namespace hype::hjs {

    namespace {

        struct NodeList {
            HjsNode* n;
            NodeList* next;
        };

        struct HjsParse {
            hjs_context* ctx;
            HjsLex lx;
            HjsTok tok;
            hcpp::u32 nodes;
            hcpp::u32 depth;  // recursion guard (16 KiB kernel stacks)
        };

        bool ok(const HjsParse& p) noexcept {
            return p.ctx->flow != F_THROW;
        }

        void fail(HjsParse& p, const char* msg) noexcept {
            hjs_err(*p.ctx, "SyntaxError", msg);
        }

        void next(HjsParse& p) noexcept { lex_next(p.lx, p.tok); }

        bool accept(HjsParse& p, HjsTK k) noexcept {
            if (!ok(p) || p.tok.kind != k) {
                return false;
            }
            next(p);
            return true;
        }

        bool expect(HjsParse& p, HjsTK k, const char* msg) noexcept {
            if (!accept(p, k)) {
                fail(p, msg);
                return false;
            }
            return true;
        }

        void semi(HjsParse& p) noexcept { (void)accept(p, TK_SEMI); }

        HjsNode* node(HjsParse& p, HjsNK k) noexcept {
            if (!ok(p)) {
                return nullptr;
            }
            if (p.nodes >= hjs_nodes_max) {
                fail(p, "script too large (node cap)");
                return nullptr;
            }
            auto* n = static_cast<HjsNode*>(
                hjs_alloc(*p.ctx, sizeof(HjsNode)));
            if (n == nullptr) {
                fail(p, "arena exhausted");
                return nullptr;
            }
            ++p.nodes;
            *n = HjsNode{};
            n->kind = k;
            return n;
        }

        HjsNode* ident_node(HjsParse& p, const char* s,
                            hcpp::u32 n) noexcept {
            HjsNode* id = node(p, N_IDENT);
            if (id != nullptr) {
                id->s = s;
                id->sn = n;
            }
            return id;
        }

        // Materializes a parsed NodeList into an arena array.
        HjsNode** list_items(HjsParse& p, NodeList* head, hcpp::u32 count,
                             hcpp::u32* out_count) noexcept {
            *out_count = 0;
            if (count == 0) {
                return nullptr;
            }
            auto** items = static_cast<HjsNode**>(
                hjs_alloc(*p.ctx, count * sizeof(HjsNode*)));
            if (items == nullptr) {
                fail(p, "arena exhausted");
                return nullptr;
            }
            *out_count = count;
            hcpp::u32 i = count;
            for (NodeList* l = head; l != nullptr; l = l->next) {
                items[--i] = l->n;  // list is newest-first; un-reverse
            }
            return items;
        }

        // ---- forward decls -----------------------------------------------------

        HjsNode* parse_expr(HjsParse& p) noexcept;
        HjsNode* parse_stmt(HjsParse& p) noexcept;
        HjsNode* parse_stmt_inner(HjsParse& p) noexcept;
        HjsNode* parse_block(HjsParse& p) noexcept;

        // ---- primary / member -----------------------------------------------------

        // Tries to parse an arrow-function parameter list starting at
        // '('; on success the cursor sits just past ')'. On any mismatch
        // the lexer state is restored and false is returned.
        bool try_arrow_paren(HjsParse& p, NodeList** params,
                             hcpp::u32* count) noexcept {
            const hcpp::u32 save_pos = p.lx.pos;
            const HjsTok save_tok = p.tok;
            *params = nullptr;
            *count = 0;
            // p.tok is '(' already; step inside.
            next(p);
            NodeList* head = nullptr;
            hcpp::u32 n = 0;
            while (ok(p) && p.tok.kind != TK_RPAREN) {
                if (p.tok.kind != TK_IDENT) {
                    goto restore;
                }
                {
                    auto* l = static_cast<NodeList*>(
                        hjs_alloc(*p.ctx, sizeof(NodeList)));
                    if (l == nullptr) {
                        goto restore;
                    }
                    l->n = ident_node(p, p.tok.p, p.tok.n);
                    l->next = head;
                    head = l;
                    ++n;
                }
                next(p);
                if (p.tok.kind == TK_COMMA) {
                    next(p);
                    continue;
                }
                break;
            }
            if (!ok(p) || p.tok.kind != TK_RPAREN) {
                goto restore;
            }
            next(p);
            if (p.tok.kind != TK_ARROW) {
                goto restore;
            }
            *params = head;
            *count = n;
            return true;
        restore:
            p.lx.pos = save_pos;
            p.tok = save_tok;
            return false;
        }

        // Parses a function body `{ stmts }` after params were consumed.
        HjsNode* parse_fn(HjsParse& p, const char* name,
                          hcpp::u32 name_len) noexcept {
            HjsNode* fn = node(p, N_FUNC);
            if (fn == nullptr) {
                return nullptr;
            }
            fn->s = name;
            fn->sn = name_len;
            if (!expect(p, TK_LPAREN, "expected ( in function")) {
                return nullptr;
            }
            NodeList* params = nullptr;
            hcpp::u32 pc = 0;
            while (ok(p) && p.tok.kind != TK_RPAREN) {
                if (p.tok.kind != TK_IDENT) {
                    fail(p, "bad parameter");
                    return nullptr;
                }
                auto* l = static_cast<NodeList*>(
                    hjs_alloc(*p.ctx, sizeof(NodeList)));
                if (l == nullptr) {
                    fail(p, "arena exhausted");
                    return nullptr;
                }
                l->n = ident_node(p, p.tok.p, p.tok.n);
                l->next = params;
                params = l;
                ++pc;
                next(p);
                if (!accept(p, TK_COMMA)) {
                    break;
                }
            }
            if (!expect(p, TK_RPAREN, "expected ) in params")) {
                return nullptr;
            }
            fn->items = list_items(p, params, pc, &fn->icount);
            fn->a = parse_block(p);
            return ok(p) ? fn : nullptr;
        }

        HjsNode* arrow_body(HjsParse& p, NodeList* params,
                            hcpp::u32 pc) noexcept {
            HjsNode* fn = node(p, N_FUNC);
            if (fn == nullptr) {
                return nullptr;
            }
            fn->items = list_items(p, params, pc, &fn->icount);
            HjsNode* blk = node(p, N_BLOCK);
            if (blk == nullptr) {
                return nullptr;
            }
            if (p.tok.kind == TK_LBRACE) {
                fn->a = parse_block(p);
                return ok(p) ? fn : nullptr;
            }
            // `x => expr` — wrap in { return expr; }.
            HjsNode* ret = node(p, N_RETURN);
            if (ret == nullptr) {
                return nullptr;
            }
            ret->a = parse_expr(p);
            auto* l = static_cast<NodeList*>(
                hjs_alloc(*p.ctx, sizeof(NodeList)));
            if (l == nullptr) {
                fail(p, "arena exhausted");
                return nullptr;
            }
            l->n = ret;
            l->next = nullptr;
            blk->items = list_items(p, l, 1, &blk->icount);
            fn->a = blk;
            return ok(p) ? fn : nullptr;
        }

        HjsNode* parse_primary(HjsParse& p) noexcept {
            if (!ok(p)) {
                return nullptr;
            }
            const HjsTok t = p.tok;
            switch (t.kind) {
                case TK_NUM: {
                    next(p);
                    HjsNode* n = node(p, N_NUM);
                    if (n != nullptr) {
                        n->num = t.num;
                    }
                    return n;
                }
                case TK_STR: {
                    next(p);
                    HjsNode* n = node(p, N_STR);
                    if (n != nullptr) {
                        // The token slice points into the lexer scratch;
                        // intern it before the next string token reuses it.
                        char* cp = hjs_str_copy(*p.ctx, t.p, t.n);
                        if (cp == nullptr) {
                            fail(p, "arena exhausted");
                            return nullptr;
                        }
                        n->s = cp;
                        n->sn = t.n;
                    }
                    return n;
                }
                case TK_TRUE:
                case TK_FALSE: {
                    next(p);
                    HjsNode* n = node(p, N_BOOL);
                    if (n != nullptr) {
                        n->num = t.kind == TK_TRUE ? 1.0 : 0.0;
                    }
                    return n;
                }
                case TK_NULL:
                    next(p);
                    return node(p, N_NULL);
                case TK_UNDEFINED:
                    next(p);
                    return node(p, N_UNDEF);
                case TK_THIS:
                    next(p);
                    return node(p, N_THIS);
                case TK_IDENT: {
                    next(p);
                    if (p.tok.kind == TK_ARROW) {  // x => ...
                        NodeList l{ident_node(p, t.p, t.n), nullptr};
                        next(p);
                        return arrow_body(p, &l, 1);
                    }
                    return ident_node(p, t.p, t.n);
                }
                case TK_LPAREN: {
                    NodeList* params = nullptr;
                    hcpp::u32 pc = 0;
                    if (try_arrow_paren(p, &params, &pc)) {
                        next(p);  // consume '=>'
                        return arrow_body(p, params, pc);
                    }
                    next(p);  // consume '('
                    HjsNode* e = parse_expr(p);
                    if (!expect(p, TK_RPAREN, "expected )")) {
                        return nullptr;
                    }
                    return e;
                }
                case TK_LBRACKET: {
                    next(p);
                    HjsNode* arr = node(p, N_ARR);
                    NodeList* head = nullptr;
                    hcpp::u32 cnt = 0;
                    while (ok(p) && p.tok.kind != TK_RBRACKET) {
                        HjsNode* elem = nullptr;
                        if (p.tok.kind == TK_COMMA) {
                            elem = node(p, N_UNDEF);  // [1,,2] hole
                        } else {
                            elem = parse_expr(p);
                            if (elem == nullptr) {
                                return nullptr;
                            }
                        }
                        auto* l = static_cast<NodeList*>(
                            hjs_alloc(*p.ctx, sizeof(NodeList)));
                        if (l == nullptr) {
                            fail(p, "arena exhausted");
                            return nullptr;
                        }
                        l->n = elem;
                        l->next = head;
                        head = l;
                        ++cnt;
                        if (!accept(p, TK_COMMA)) {
                            break;
                        }
                    }
                    if (!expect(p, TK_RBRACKET, "expected ] in array")) {
                        return nullptr;
                    }
                    if (arr != nullptr) {
                        arr->items = list_items(p, head, cnt, &arr->icount);
                    }
                    return ok(p) ? arr : nullptr;
                }
                case TK_LBRACE: {
                    next(p);
                    HjsNode* obj = node(p, N_OBJ);
                    NodeList* head = nullptr;
                    hcpp::u32 cnt = 0;
                    while (ok(p) && p.tok.kind != TK_RBRACE) {
                        const char* key = nullptr;
                        hcpp::u32 key_len = 0;
                        if (p.tok.kind == TK_IDENT ||
                            (p.tok.kind >= TK_VAR &&
                             p.tok.kind <= TK_INSTANCEOF)) {
                            key = p.tok.p;
                            key_len = p.tok.n;
                        } else if (p.tok.kind == TK_STR) {
                            key = hjs_str_copy(*p.ctx, p.tok.p, p.tok.n);
                            key_len = p.tok.n;
                            if (key == nullptr) {
                                fail(p, "arena exhausted");
                                return nullptr;
                            }
                        } else if (p.tok.kind == TK_NUM) {
                            char nb[24];
                            const hcpp::u32 nn =
                                num_to_str(p.tok.num, nb, sizeof(nb));
                            key = hjs_str_copy(*p.ctx, nb, nn);
                            key_len = nn;
                            if (key == nullptr) {
                                fail(p, "arena exhausted");
                                return nullptr;
                            }
                        } else {
                            fail(p, "bad object key");
                            return nullptr;
                        }
                        HjsNode* kn = node(p, N_STR);
                        HjsNode* val = nullptr;
                        const HjsTok key_tok = p.tok;
                        next(p);
                        if (accept(p, TK_COLON)) {
                            val = parse_expr(p);
                        } else {
                            // {a} shorthand -> {a: a}
                            val = ident_node(p, key, key_len);
                        }
                        (void)key_tok;
                        if (kn == nullptr || val == nullptr) {
                            return nullptr;
                        }
                        kn->s = key;
                        kn->sn = key_len;
                        // Push key then value; the un-reverse in
                        // list_items yields [k0,v0,k1,v1,...].
                        for (int i = 0; i < 2; ++i) {
                            auto* l = static_cast<NodeList*>(
                                hjs_alloc(*p.ctx, sizeof(NodeList)));
                            if (l == nullptr) {
                                fail(p, "arena exhausted");
                                return nullptr;
                            }
                            l->n = i == 0 ? kn : val;
                            l->next = head;
                            head = l;
                            ++cnt;
                        }
                        if (!accept(p, TK_COMMA)) {
                            break;
                        }
                    }
                    if (!expect(p, TK_RBRACE, "expected } in object")) {
                        return nullptr;
                    }
                    if (obj != nullptr) {
                        obj->items = list_items(p, head, cnt, &obj->icount);
                    }
                    return ok(p) ? obj : nullptr;
                }
                case TK_FUNCTION: {
                    next(p);
                    const char* name = "";
                    hcpp::u32 name_len = 0;
                    if (p.tok.kind == TK_IDENT) {
                        name = p.tok.p;
                        name_len = p.tok.n;
                        next(p);
                    }
                    return parse_fn(p, name, name_len);
                }
                default:
                    fail(p, "unexpected token");
                    return nullptr;
            }
        }

        HjsNode* parse_member(HjsParse& p) noexcept {
            HjsNode* e = parse_primary(p);
            while (ok(p)) {
                if (accept(p, TK_DOT)) {
                    if (p.tok.kind != TK_IDENT &&
                        !(p.tok.kind >= TK_VAR &&
                          p.tok.kind <= TK_INSTANCEOF)) {
                        fail(p, "expected property name");
                        return nullptr;
                    }
                    HjsNode* m = node(p, N_MEMBER);
                    if (m == nullptr) {
                        return nullptr;
                    }
                    m->a = e;
                    m->s = p.tok.p;
                    m->sn = p.tok.n;
                    next(p);
                    e = m;
                    continue;
                }
                if (accept(p, TK_LBRACKET)) {
                    HjsNode* ix = node(p, N_INDEX);
                    if (ix == nullptr) {
                        return nullptr;
                    }
                    ix->a = e;
                    ix->b = parse_expr(p);
                    if (!expect(p, TK_RBRACKET, "expected ] in index")) {
                        return nullptr;
                    }
                    e = ix;
                    continue;
                }
                if (accept(p, TK_LPAREN)) {
                    HjsNode* call = node(p, N_CALL);
                    NodeList* head = nullptr;
                    hcpp::u32 cnt = 0;
                    while (ok(p) && p.tok.kind != TK_RPAREN) {
                        HjsNode* arg = parse_expr(p);
                        if (arg == nullptr) {
                            return nullptr;
                        }
                        auto* l = static_cast<NodeList*>(
                            hjs_alloc(*p.ctx, sizeof(NodeList)));
                        if (l == nullptr) {
                            fail(p, "arena exhausted");
                            return nullptr;
                        }
                        l->n = arg;
                        l->next = head;
                        head = l;
                        ++cnt;
                        if (!accept(p, TK_COMMA)) {
                            break;
                        }
                    }
                    if (!expect(p, TK_RPAREN, "expected ) in call")) {
                        return nullptr;
                    }
                    if (call != nullptr) {
                        call->a = e;
                        call->items =
                            list_items(p, head, cnt, &call->icount);
                    }
                    e = call;
                    continue;
                }
                break;
            }
            return ok(p) ? e : nullptr;
        }

        bool is_assignable(const HjsNode* n) noexcept {
            return n != nullptr &&
                   (n->kind == N_IDENT || n->kind == N_MEMBER ||
                    n->kind == N_INDEX);
        }

        HjsNode* parse_unary(HjsParse& p) noexcept {
            if (!ok(p)) {
                return nullptr;
            }
            switch (p.tok.kind) {
                case TK_BANG:
                case TK_TILDE:
                case TK_MINUS:
                case TK_PLUS:
                case TK_TYPEOF:
                case TK_VOID:
                case TK_DELETE: {
                    const HjsTK op = p.tok.kind;
                    next(p);
                    HjsNode* n = node(p, N_UNARY);
                    if (n == nullptr) {
                        return nullptr;
                    }
                    n->op = static_cast<hcpp::u8>(op);
                    n->a = parse_unary(p);
                    return ok(p) ? n : nullptr;
                }
                case TK_INCR:
                case TK_DECR: {
                    const HjsTK op = p.tok.kind;
                    next(p);
                    HjsNode* n = node(p, N_UNARY);
                    if (n == nullptr) {
                        return nullptr;
                    }
                    n->op = static_cast<hcpp::u8>(op);
                    n->a = parse_unary(p);
                    if (!is_assignable(n->a)) {
                        fail(p, "bad ++/-- target");
                        return nullptr;
                    }
                    return n;
                }
                case TK_NEW: {
                    next(p);
                    HjsNode* callee = parse_member(p);
                    if (callee == nullptr) {
                        return nullptr;
                    }
                    if (callee->kind == N_CALL) {
                        callee->aux = 1;  // new F(args)
                        return callee;
                    }
                    HjsNode* call = node(p, N_CALL);  // bare `new F`
                    if (call == nullptr) {
                        return nullptr;
                    }
                    call->a = callee;
                    call->aux = 1;
                    return call;
                }
                default:
                    break;
            }
            HjsNode* e = parse_member(p);
            if (ok(p) &&
                (p.tok.kind == TK_INCR || p.tok.kind == TK_DECR)) {
                if (!is_assignable(e)) {
                    fail(p, "bad ++/-- target");
                    return nullptr;
                }
                HjsNode* n = node(p, N_POSTFIX);
                if (n == nullptr) {
                    return nullptr;
                }
                n->op = static_cast<hcpp::u8>(p.tok.kind);
                n->a = e;
                next(p);
                return n;
            }
            return e;
        }

        HjsNode* bin_chain(HjsParse& p, HjsNode* (*sub)(HjsParse&),
                           const HjsTK* ops, hcpp::u32 nops) noexcept {
            HjsNode* l = sub(p);
            while (ok(p)) {
                HjsTK op = p.tok.kind;
                bool match = false;
                for (hcpp::u32 i = 0; i < nops; ++i) {
                    if (ops[i] == op) {
                        match = true;
                        break;
                    }
                }
                if (!match) {
                    break;
                }
                next(p);
                HjsNode* n = node(p, N_BIN);
                if (n == nullptr) {
                    return nullptr;
                }
                n->op = static_cast<hcpp::u8>(op);
                n->a = l;
                n->b = sub(p);
                if (n->b == nullptr) {
                    return nullptr;
                }
                l = n;
            }
            return ok(p) ? l : nullptr;
        }

        HjsNode* parse_mult(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_STAR, TK_SLASH, TK_PERCENT};
            return bin_chain(p, &parse_unary, ops, 3);
        }

        HjsNode* parse_add(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_PLUS, TK_MINUS};
            return bin_chain(p, &parse_mult, ops, 2);
        }

        HjsNode* parse_shift(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_SHL, TK_SHR, TK_USHR};
            return bin_chain(p, &parse_add, ops, 3);
        }

        HjsNode* parse_rel(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_LT, TK_GT, TK_LE, TK_GE, TK_IN,
                                 TK_INSTANCEOF};
            return bin_chain(p, &parse_shift, ops, 6);
        }

        HjsNode* parse_eq(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_EQEQ, TK_NOTEQ, TK_EQEQEQ, TK_NOTEQEQ};
            return bin_chain(p, &parse_rel, ops, 4);
        }

        HjsNode* parse_bitand(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_AMP};
            return bin_chain(p, &parse_eq, ops, 1);
        }

        HjsNode* parse_bitxor(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_CARET};
            return bin_chain(p, &parse_bitand, ops, 1);
        }

        HjsNode* parse_bitor(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_PIPE};
            return bin_chain(p, &parse_bitxor, ops, 1);
        }

        HjsNode* parse_and(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_ANDAND};
            return bin_chain(p, &parse_bitor, ops, 1);
        }

        HjsNode* parse_or(HjsParse& p) noexcept {
            const HjsTK ops[] = {TK_OROR};
            return bin_chain(p, &parse_and, ops, 1);
        }

        HjsNode* parse_cond(HjsParse& p) noexcept {
            HjsNode* c = parse_or(p);
            if (ok(p) && accept(p, TK_QUESTION)) {
                HjsNode* n = node(p, N_COND);
                if (n == nullptr) {
                    return nullptr;
                }
                n->a = c;
                n->b = parse_expr(p);
                if (!expect(p, TK_COLON, "expected : in ?:")) {
                    return nullptr;
                }
                n->c = parse_expr(p);
                return ok(p) ? n : nullptr;
            }
            return c;
        }

        constexpr hcpp::u32 k_parse_depth = 128;

        HjsNode* parse_expr(HjsParse& p) noexcept {
            if (p.depth >= k_parse_depth) {
                fail(p, "nesting too deep");
                return nullptr;
            }
            ++p.depth;
            HjsNode* t = parse_cond(p);
            if (!ok(p)) {
                --p.depth;
                return nullptr;
            }
            switch (p.tok.kind) {
                case TK_ASSIGN:
                case TK_PLUS_EQ:
                case TK_MINUS_EQ:
                case TK_STAR_EQ:
                case TK_SLASH_EQ:
                case TK_PCT_EQ: {
                    if (!is_assignable(t)) {
                        fail(p, "bad assignment target");
                        --p.depth;
                        return nullptr;
                    }
                    const HjsTK op = p.tok.kind;
                    next(p);
                    HjsNode* n = node(p, N_ASSIGN);
                    if (n == nullptr) {
                        --p.depth;
                        return nullptr;
                    }
                    n->op = static_cast<hcpp::u8>(op);
                    n->a = t;
                    n->b = parse_expr(p);  // right-associative
                    --p.depth;
                    return ok(p) ? n : nullptr;
                }
                default:
                    --p.depth;
                    return t;
            }
        }

        // ---- statements --------------------------------------------------------------

        HjsNode* parse_var_tail(HjsParse& p, HjsTK decl_kind) noexcept {
            // Parses "a = expr, b" after the keyword — returns a single
            // N_VARDECL or an N_BLOCK of them.
            NodeList* head = nullptr;
            hcpp::u32 cnt = 0;
            while (ok(p)) {
                if (p.tok.kind != TK_IDENT) {
                    fail(p, "expected variable name");
                    return nullptr;
                }
                const char* name = p.tok.p;
                const hcpp::u32 name_len = p.tok.n;
                next(p);
                HjsNode* d = node(p, N_VARDECL);
                if (d == nullptr) {
                    return nullptr;
                }
                d->op = static_cast<hcpp::u8>(decl_kind);
                d->s = name;
                d->sn = name_len;
                if (accept(p, TK_ASSIGN)) {
                    d->a = parse_expr(p);
                }
                auto* l = static_cast<NodeList*>(
                    hjs_alloc(*p.ctx, sizeof(NodeList)));
                if (l == nullptr) {
                    fail(p, "arena exhausted");
                    return nullptr;
                }
                l->n = d;
                l->next = head;
                head = l;
                ++cnt;
                if (!accept(p, TK_COMMA)) {
                    break;
                }
            }
            if (!ok(p)) {
                return nullptr;
            }
            if (cnt == 1) {
                return head->n;
            }
            HjsNode* blk = node(p, N_BLOCK);
            if (blk != nullptr) {
                blk->items = list_items(p, head, cnt, &blk->icount);
            }
            return blk;
        }

        HjsNode* parse_block(HjsParse& p) noexcept {
            if (!expect(p, TK_LBRACE, "expected {")) {
                return nullptr;
            }
            HjsNode* blk = node(p, N_BLOCK);
            NodeList* head = nullptr;
            hcpp::u32 cnt = 0;
            while (ok(p) && p.tok.kind != TK_RBRACE &&
                   p.tok.kind != TK_EOF) {
                HjsNode* s = parse_stmt(p);
                if (s == nullptr) {
                    return nullptr;
                }
                auto* l = static_cast<NodeList*>(
                    hjs_alloc(*p.ctx, sizeof(NodeList)));
                if (l == nullptr) {
                    fail(p, "arena exhausted");
                    return nullptr;
                }
                l->n = s;
                l->next = head;
                head = l;
                ++cnt;
            }
            if (!expect(p, TK_RBRACE, "expected }")) {
                return nullptr;
            }
            if (blk != nullptr) {
                blk->items = list_items(p, head, cnt, &blk->icount);
            }
            return ok(p) ? blk : nullptr;
        }

        HjsNode* parse_for(HjsParse& p) noexcept {
            next(p);  // 'for'
            if (!expect(p, TK_LPAREN, "expected ( after for")) {
                return nullptr;
            }
            // Look for the for-of shape: (var|let|const)? IDENT 'of'.
            const hcpp::u32 save_pos = p.lx.pos;
            const HjsTok save_tok = p.tok;
            HjsTK decl = TK_EOF;
            if (p.tok.kind == TK_VAR || p.tok.kind == TK_LET ||
                p.tok.kind == TK_CONST) {
                decl = p.tok.kind;
                next(p);
            }
            if (p.tok.kind == TK_IDENT) {
                const char* vname = p.tok.p;
                const hcpp::u32 vlen = p.tok.n;
                next(p);
                if (p.tok.kind == TK_OF) {
                    next(p);
                    HjsNode* fo = node(p, N_FOROF);
                    if (fo == nullptr) {
                        return nullptr;
                    }
                    fo->s = vname;
                    fo->sn = vlen;
                    fo->op = decl == TK_EOF
                                 ? 0
                                 : static_cast<hcpp::u8>(decl);
                    fo->a = parse_expr(p);
                    if (!expect(p, TK_RPAREN, "expected ) in for-of")) {
                        return nullptr;
                    }
                    fo->b = parse_stmt(p);
                    return ok(p) ? fo : nullptr;
                }
            }
            // Not for-of — restore and parse the classic form.
            p.lx.pos = save_pos;
            p.tok = save_tok;
            HjsNode* f = node(p, N_FOR);
            auto** items = static_cast<HjsNode**>(
                hjs_alloc(*p.ctx, 4 * sizeof(HjsNode*)));
            if (f == nullptr || items == nullptr) {
                fail(p, "arena exhausted");
                return nullptr;
            }
            f->items = items;
            f->icount = 4;
            // init
            if (p.tok.kind == TK_VAR || p.tok.kind == TK_LET ||
                p.tok.kind == TK_CONST) {
                const HjsTK dk = p.tok.kind;
                next(p);
                items[0] = parse_var_tail(p, dk);
            } else if (p.tok.kind == TK_SEMI) {
                items[0] = node(p, N_EMPTY);
            } else {
                HjsNode* es = node(p, N_EXPRSTMT);
                if (es == nullptr) {
                    return nullptr;
                }
                es->a = parse_expr(p);
                items[0] = es;
            }
            if (!expect(p, TK_SEMI, "expected ; in for")) {
                return nullptr;
            }
            items[1] = p.tok.kind == TK_SEMI ? node(p, N_EMPTY)
                                             : parse_expr(p);
            if (!expect(p, TK_SEMI, "expected ; in for")) {
                return nullptr;
            }
            items[2] = p.tok.kind == TK_RPAREN ? node(p, N_EMPTY)
                                               : parse_expr(p);
            if (!expect(p, TK_RPAREN, "expected ) in for")) {
                return nullptr;
            }
            items[3] = parse_stmt(p);
            return ok(p) ? f : nullptr;
        }

        HjsNode* parse_stmt(HjsParse& p) noexcept {
            if (!ok(p)) {
                return nullptr;
            }
            if (p.depth >= k_parse_depth) {
                fail(p, "nesting too deep");
                return nullptr;
            }
            ++p.depth;
            HjsNode* r = parse_stmt_inner(p);
            --p.depth;
            return r;
        }

        HjsNode* parse_stmt_inner(HjsParse& p) noexcept {
            switch (p.tok.kind) {
                case TK_SEMI:
                    next(p);
                    return node(p, N_EMPTY);
                case TK_LBRACE:
                    return parse_block(p);
                case TK_VAR:
                case TK_LET:
                case TK_CONST: {
                    const HjsTK dk = p.tok.kind;
                    next(p);
                    HjsNode* d = parse_var_tail(p, dk);
                    semi(p);
                    return d;
                }
                case TK_FUNCTION: {
                    next(p);
                    if (p.tok.kind != TK_IDENT) {
                        fail(p, "function name required");
                        return nullptr;
                    }
                    const char* name = p.tok.p;
                    const hcpp::u32 name_len = p.tok.n;
                    next(p);
                    HjsNode* d = node(p, N_VARDECL);
                    if (d == nullptr) {
                        return nullptr;
                    }
                    d->op = static_cast<hcpp::u8>(TK_VAR);
                    d->s = name;
                    d->sn = name_len;
                    d->a = parse_fn(p, name, name_len);
                    return ok(p) ? d : nullptr;
                }
                case TK_IF: {
                    next(p);
                    HjsNode* n = node(p, N_IF);
                    if (!expect(p, TK_LPAREN, "expected ( after if")) {
                        return nullptr;
                    }
                    if (n != nullptr) {
                        n->a = parse_expr(p);
                    }
                    if (!expect(p, TK_RPAREN, "expected ) in if")) {
                        return nullptr;
                    }
                    if (n != nullptr) {
                        n->b = parse_stmt(p);
                        if (accept(p, TK_ELSE)) {
                            n->c = parse_stmt(p);
                        }
                    }
                    return ok(p) ? n : nullptr;
                }
                case TK_WHILE: {
                    next(p);
                    HjsNode* n = node(p, N_WHILE);
                    if (!expect(p, TK_LPAREN, "expected ( after while")) {
                        return nullptr;
                    }
                    if (n != nullptr) {
                        n->a = parse_expr(p);
                    }
                    if (!expect(p, TK_RPAREN, "expected ) in while")) {
                        return nullptr;
                    }
                    if (n != nullptr) {
                        n->b = parse_stmt(p);
                    }
                    return ok(p) ? n : nullptr;
                }
                case TK_DO: {
                    next(p);
                    HjsNode* n = node(p, N_DOWHILE);
                    if (n != nullptr) {
                        n->b = parse_stmt(p);
                    }
                    if (!expect(p, TK_WHILE, "expected while after do") ||
                        !expect(p, TK_LPAREN, "expected ( in do-while")) {
                        return nullptr;
                    }
                    if (n != nullptr) {
                        n->a = parse_expr(p);
                    }
                    if (!expect(p, TK_RPAREN, "expected ) in do-while")) {
                        return nullptr;
                    }
                    semi(p);
                    return ok(p) ? n : nullptr;
                }
                case TK_FOR:
                    return parse_for(p);
                case TK_RETURN: {
                    next(p);
                    HjsNode* n = node(p, N_RETURN);
                    if (p.tok.kind != TK_SEMI &&
                        p.tok.kind != TK_RBRACE && p.tok.kind != TK_EOF) {
                        if (n != nullptr) {
                            n->a = parse_expr(p);
                        }
                    }
                    semi(p);
                    return ok(p) ? n : nullptr;
                }
                case TK_BREAK:
                    next(p);
                    semi(p);
                    return node(p, N_BREAK);
                case TK_CONTINUE:
                    next(p);
                    semi(p);
                    return node(p, N_CONTINUE);
                case TK_THROW: {
                    next(p);
                    HjsNode* n = node(p, N_THROW);
                    if (n != nullptr) {
                        n->a = parse_expr(p);
                    }
                    semi(p);
                    return ok(p) ? n : nullptr;
                }
                case TK_TRY: {
                    next(p);
                    HjsNode* n = node(p, N_TRY);
                    if (n != nullptr) {
                        n->a = parse_block(p);
                    }
                    if (!ok(p)) {
                        return nullptr;
                    }
                    if (accept(p, TK_CATCH)) {
                        if (accept(p, TK_LPAREN)) {
                            if (p.tok.kind != TK_IDENT) {
                                fail(p, "catch needs a name");
                                return nullptr;
                            }
                            if (n != nullptr) {
                                n->s = p.tok.p;
                                n->sn = p.tok.n;
                            }
                            next(p);
                            if (!expect(p, TK_RPAREN,
                                        "expected ) in catch")) {
                                return nullptr;
                            }
                        }
                        if (n != nullptr) {
                            n->b = parse_block(p);
                        }
                    }
                    if (accept(p, TK_FINALLY)) {
                        if (n != nullptr) {
                            n->c = parse_block(p);
                        }
                    }
                    if (n != nullptr && n->b == nullptr && n->c == nullptr) {
                        fail(p, "try needs catch or finally");
                        return nullptr;
                    }
                    return ok(p) ? n : nullptr;
                }
                default: {
                    HjsNode* es = node(p, N_EXPRSTMT);
                    if (es == nullptr) {
                        return nullptr;
                    }
                    es->a = parse_expr(p);
                    semi(p);
                    return ok(p) ? es : nullptr;
                }
            }
        }

    }  // namespace

    bool hjs_parse(hjs_context& ctx, const char* src, hcpp::u32 len,
                   HjsNode** root) noexcept {
        HjsParse p{};
        p.ctx = &ctx;
        p.lx.ctx = &ctx;
        p.lx.src = src;
        p.lx.len = len;
        p.lx.pos = 0;
        // String-escape scratch: a decoded literal can never exceed the
        // source length, and is bounded so huge inputs still fit.
        const hcpp::u32 cap = len < 8192 ? len + 1 : 8192;
        p.lx.str_buf =
            static_cast<char*>(hjs_alloc(ctx, cap));
        p.lx.str_cap = p.lx.str_buf != nullptr ? cap : 0;
        if (p.lx.str_buf == nullptr && len != 0) {
            hjs_err(ctx, "SyntaxError", "arena exhausted");
            return false;
        }
        p.lx.str_len = 0;
        next(p);
        HjsNode* blk = node(p, N_BLOCK);
        NodeList* head = nullptr;
        hcpp::u32 cnt = 0;
        while (ok(p) && p.tok.kind != TK_EOF) {
            HjsNode* s = parse_stmt(p);
            if (s == nullptr) {
                return false;
            }
            auto* l = static_cast<NodeList*>(hjs_alloc(ctx, sizeof(NodeList)));
            if (l == nullptr) {
                hjs_err(ctx, "SyntaxError", "arena exhausted");
                return false;
            }
            l->n = s;
            l->next = head;
            head = l;
            ++cnt;
        }
        if (!ok(p)) {
            return false;
        }
        if (blk == nullptr) {
            hjs_err(ctx, "SyntaxError", "arena exhausted");
            return false;
        }
        blk->items = list_items(p, head, cnt, &blk->icount);
        *root = blk;
        return ctx.flow != F_THROW;
    }

}  // namespace hype::hjs
