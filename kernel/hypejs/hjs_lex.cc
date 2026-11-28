// hypejs lexer — ES-subset tokenizer (issue #181).
//
// Produces HjsTok slices into the source: numbers (int/hex/float/exp),
// strings with the usual backslash escapes, identifiers/keywords and the
// punctuator set the parser understands. // and /* */ comments are
// skipped; a lexical failure lands TK_EOF after setting ctx.error.

#include "kernel/hypejs/hjs_int.h"

namespace hype::hjs {

    namespace {

        struct Kw {
            const char* text;
            HjsTK kind;
        };

        constexpr Kw k_keywords[] = {
            {"var", TK_VAR},         {"let", TK_LET},
            {"const", TK_CONST},     {"function", TK_FUNCTION},
            {"return", TK_RETURN},   {"if", TK_IF},
            {"else", TK_ELSE},       {"while", TK_WHILE},
            {"do", TK_DO},           {"for", TK_FOR},
            {"of", TK_OF},           {"in", TK_IN},
            {"break", TK_BREAK},     {"continue", TK_CONTINUE},
            {"true", TK_TRUE},       {"false", TK_FALSE},
            {"null", TK_NULL},       {"undefined", TK_UNDEFINED},
            {"typeof", TK_TYPEOF},   {"void", TK_VOID},
            {"delete", TK_DELETE},   {"this", TK_THIS},
            {"throw", TK_THROW},     {"try", TK_TRY},
            {"catch", TK_CATCH},     {"finally", TK_FINALLY},
            {"new", TK_NEW},         {"instanceof", TK_INSTANCEOF},
        };

        bool is_ident0(char c) noexcept {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == '_' || c == '$';
        }

        bool is_ident(char c) noexcept {
            return is_ident0(c) || (c >= '0' && c <= '9');
        }

        bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

        bool is_space(char c) noexcept {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                   c == '\v' || c == '\f';
        }

        char hex_val(char c) noexcept {
            if (c >= '0' && c <= '9') {
                return static_cast<char>(c - '0');
            }
            if (c >= 'a' && c <= 'f') {
                return static_cast<char>(c - 'a' + 10);
            }
            if (c >= 'A' && c <= 'F') {
                return static_cast<char>(c - 'A' + 10);
            }
            return -1;
        }

    }  // namespace

    static void lex_fail(HjsLex& lx, const char* msg) noexcept {
        hjs_err(*lx.ctx, "SyntaxError", msg);
    }

    static void skip_ws(HjsLex& lx) noexcept {
        for (;;) {
            while (lx.pos < lx.len && is_space(lx.src[lx.pos])) {
                ++lx.pos;
            }
            if (lx.pos + 1 < lx.len && lx.src[lx.pos] == '/' &&
                lx.src[lx.pos + 1] == '/') {
                while (lx.pos < lx.len && lx.src[lx.pos] != '\n') {
                    ++lx.pos;
                }
                continue;
            }
            if (lx.pos + 1 < lx.len && lx.src[lx.pos] == '/' &&
                lx.src[lx.pos + 1] == '*') {
                lx.pos += 2;
                while (lx.pos + 1 < lx.len &&
                       !(lx.src[lx.pos] == '*' && lx.src[lx.pos + 1] == '/')) {
                    ++lx.pos;
                }
                lx.pos = lx.pos + 2 <= lx.len ? lx.pos + 2 : lx.len;
                continue;
            }
            return;
        }
    }

    static void lex_number(HjsLex& lx, HjsTok& t) noexcept {
        const char* s = lx.src;
        const hcpp::u32 start = lx.pos;
        double v = 0.0;
        if (lx.pos + 1 < lx.len && s[lx.pos] == '0' &&
            (s[lx.pos + 1] == 'x' || s[lx.pos + 1] == 'X')) {
            lx.pos += 2;
            while (lx.pos < lx.len) {
                const char h = hex_val(s[lx.pos]);
                if (h < 0) {
                    break;
                }
                v = v * 16.0 + static_cast<double>(h);
                ++lx.pos;
            }
            t.num = v;
            t.p = s + start;
            t.n = lx.pos - start;
            return;
        }
        while (lx.pos < lx.len && is_digit(s[lx.pos])) {
            v = v * 10.0 + static_cast<double>(s[lx.pos] - '0');
            ++lx.pos;
        }
        if (lx.pos < lx.len && s[lx.pos] == '.' &&
            lx.pos + 1 < lx.len && is_digit(s[lx.pos + 1])) {
            ++lx.pos;
            double scale = 0.1;
            while (lx.pos < lx.len && is_digit(s[lx.pos])) {
                v += scale * static_cast<double>(s[lx.pos] - '0');
                scale *= 0.1;
                ++lx.pos;
            }
        }
        if (lx.pos < lx.len && (s[lx.pos] == 'e' || s[lx.pos] == 'E')) {
            hcpp::u32 save = lx.pos;
            ++lx.pos;
            bool neg = false;
            if (lx.pos < lx.len &&
                (s[lx.pos] == '+' || s[lx.pos] == '-')) {
                neg = s[lx.pos] == '-';
                ++lx.pos;
            }
            if (lx.pos < lx.len && is_digit(s[lx.pos])) {
                hcpp::u32 e = 0;
                while (lx.pos < lx.len && is_digit(s[lx.pos])) {
                    e = e * 10 + static_cast<hcpp::u32>(s[lx.pos] - '0');
                    ++lx.pos;
                }
                double mult = 1.0;
                for (hcpp::u32 i = 0; i < e && i < 320; ++i) {
                    mult *= 10.0;
                }
                v = neg ? v / mult : v * mult;
            } else {
                lx.pos = save;  // "1e" is not a number tail
            }
        }
        t.num = v;
        t.p = s + start;
        t.n = lx.pos - start;
    }

    static bool lex_string(HjsLex& lx, HjsTok& t) noexcept {
        const char quote = lx.src[lx.pos];
        const hcpp::u32 start = lx.pos;
        ++lx.pos;
        lx.str_len = 0;
        while (lx.pos < lx.len && lx.src[lx.pos] != quote) {
            char c = lx.src[lx.pos++];
            if (c == '\\' && lx.pos < lx.len) {
                const char e = lx.src[lx.pos++];
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'v': c = '\v'; break;
                    case '0': c = '\0'; break;
                    case 'x': {
                        if (lx.pos + 1 < lx.len) {
                            const char h1 = hex_val(lx.src[lx.pos]);
                            const char h2 = hex_val(lx.src[lx.pos + 1]);
                            if (h1 >= 0 && h2 >= 0) {
                                c = static_cast<char>(h1 * 16 + h2);
                                lx.pos += 2;
                                break;
                            }
                        }
                        c = 'x';
                        break;
                    }
                    case 'u': {
                        // \uNNNN -> ASCII when low, '?' otherwise (the
                        // engine is byte-oriented; no UTF-16).
                        hcpp::u32 cp = 0;
                        hcpp::u32 k = 0;
                        for (; k < 4 && lx.pos < lx.len; ++k) {
                            const char h = hex_val(lx.src[lx.pos]);
                            if (h < 0) {
                                break;
                            }
                            cp = cp * 16 + static_cast<hcpp::u32>(h);
                            ++lx.pos;
                        }
                        c = k == 4 ? (cp < 128 ? static_cast<char>(cp)
                                               : '?')
                                   : 'u';
                        break;
                    }
                    case '\n':
                        continue;  // line continuation
                    default:
                        c = e;  // \' \" \\ and unknown -> literal char
                        break;
                }
            }
            if (lx.str_len < lx.str_cap) {
                lx.str_buf[lx.str_len++] = c;
            }
        }
        if (lx.pos >= lx.len) {
            lex_fail(lx, "unterminated string");
            return false;
        }
        ++lx.pos;  // closing quote
        t.p = lx.str_buf;
        t.n = lx.str_len;
        t.num = 0;
        (void)start;
        return true;
    }

    static void lex_ident(HjsLex& lx, HjsTok& t) noexcept {
        const hcpp::u32 start = lx.pos;
        while (lx.pos < lx.len && is_ident(lx.src[lx.pos])) {
            ++lx.pos;
        }
        t.p = lx.src + start;
        t.n = lx.pos - start;
        t.kind = TK_IDENT;
        for (hcpp::u32 i = 0; i < sizeof(k_keywords) / sizeof(k_keywords[0]);
             ++i) {
            if (str_eqz(t.p, t.n, k_keywords[i].text)) {
                t.kind = k_keywords[i].kind;
                return;
            }
        }
    }

    void lex_next(HjsLex& lx, HjsTok& t) noexcept {
        skip_ws(lx);
        t = HjsTok{};
        t.kind = TK_EOF;
        if (lx.pos >= lx.len || lx.ctx->flow == F_THROW) {
            return;
        }
        const char c = lx.src[lx.pos];
        t.p = lx.src + lx.pos;
        t.n = 1;
        if (is_digit(c) ||
            (c == '.' && lx.pos + 1 < lx.len && is_digit(lx.src[lx.pos + 1]))) {
            t.kind = TK_NUM;
            lex_number(lx, t);
            return;
        }
        if (c == '"' || c == '\'') {
            t.kind = TK_STR;
            lex_string(lx, t);
            return;
        }
        if (is_ident0(c)) {
            lex_ident(lx, t);
            return;
        }
        ++lx.pos;
        const char n1 = lx.pos < lx.len ? lx.src[lx.pos] : '\0';
        const char n2 = lx.pos + 1 < lx.len ? lx.src[lx.pos + 1] : '\0';
        switch (c) {
            case '(':
                t.kind = TK_LPAREN;
                return;
            case ')':
                t.kind = TK_RPAREN;
                return;
            case '{':
                t.kind = TK_LBRACE;
                return;
            case '}':
                t.kind = TK_RBRACE;
                return;
            case '[':
                t.kind = TK_LBRACKET;
                return;
            case ']':
                t.kind = TK_RBRACKET;
                return;
            case ';':
                t.kind = TK_SEMI;
                return;
            case ',':
                t.kind = TK_COMMA;
                return;
            case ':':
                t.kind = TK_COLON;
                return;
            case '?':
                t.kind = TK_QUESTION;
                return;
            case '~':
                t.kind = TK_TILDE;
                return;
            case '.':
                t.kind = TK_DOT;
                return;
            case '=':
                if (n1 == '=') {
                    ++lx.pos;
                    if (n2 == '=') {
                        ++lx.pos;
                        t.kind = TK_EQEQEQ;
                    } else {
                        t.kind = TK_EQEQ;
                    }
                } else if (n1 == '>') {
                    ++lx.pos;
                    t.kind = TK_ARROW;
                } else {
                    t.kind = TK_ASSIGN;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '!':
                if (n1 == '=') {
                    ++lx.pos;
                    if (n2 == '=') {
                        ++lx.pos;
                        t.kind = TK_NOTEQEQ;
                    } else {
                        t.kind = TK_NOTEQ;
                    }
                } else {
                    t.kind = TK_BANG;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '<':
                if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_LE;
                } else if (n1 == '<') {
                    ++lx.pos;
                    t.kind = TK_SHL;
                } else {
                    t.kind = TK_LT;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '>':
                if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_GE;
                } else if (n1 == '>') {
                    ++lx.pos;
                    if (n2 == '>') {
                        ++lx.pos;
                        t.kind = TK_USHR;
                    } else {
                        t.kind = TK_SHR;
                    }
                } else {
                    t.kind = TK_GT;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '&':
                if (n1 == '&') {
                    ++lx.pos;
                    t.kind = TK_ANDAND;
                } else {
                    t.kind = TK_AMP;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '|':
                if (n1 == '|') {
                    ++lx.pos;
                    t.kind = TK_OROR;
                } else {
                    t.kind = TK_PIPE;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '^':
                t.kind = TK_CARET;
                return;
            case '+':
                if (n1 == '+') {
                    ++lx.pos;
                    t.kind = TK_INCR;
                } else if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_PLUS_EQ;
                } else {
                    t.kind = TK_PLUS;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '-':
                if (n1 == '-') {
                    ++lx.pos;
                    t.kind = TK_DECR;
                } else if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_MINUS_EQ;
                } else {
                    t.kind = TK_MINUS;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '*':
                if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_STAR_EQ;
                } else {
                    t.kind = TK_STAR;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '/':
                if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_SLASH_EQ;
                } else {
                    t.kind = TK_SLASH;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            case '%':
                if (n1 == '=') {
                    ++lx.pos;
                    t.kind = TK_PCT_EQ;
                } else {
                    t.kind = TK_PERCENT;
                }
                t.n = lx.pos - (t.p - lx.src);
                return;
            default:
                lex_fail(lx, "unexpected character");
                return;
        }
    }

}  // namespace hype::hjs
