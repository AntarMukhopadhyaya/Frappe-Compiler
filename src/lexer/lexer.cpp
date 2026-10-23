//
// Created by antar on 19/10/26.
//
#include <frappe/lexer/lexer.hpp>
#include <format>
#include <utility>


namespace frappe::lexer {
    namespace {
        [[nodiscard]] inline bool is_at_end(const Lexer *l) noexcept {
            return l->current >= l->source.size();
        }

        [[nodiscard]] inline char peek(const Lexer *l) noexcept {
            return is_at_end(l) ? '\0' : l->source[l->current];
        }

        [[nodiscard]] inline char peek_next(const Lexer *l) noexcept {
            return (l->current + 1 >= l->source.size()) ? '\0' : l->source[l->current + 1];
        }

        inline char advance(Lexer *l) noexcept {
            char c = l->source[l->current];
            ++l->current;
            ++l->column;
            return c;
        }

        [[nodiscard]] source::Span current_span(const Lexer *l) noexcept {
            return source::Span{
                .start = source::Location{l->start, l->token_line, l->token_column},
                .end = source::Location{l->current, l->line, l->column},
            };
        }

        inline bool match(Lexer *l, char expected) noexcept {
            if (is_at_end(l) || l->source[l->current] != expected) return false;
            ++l->current;
            ++l->column;
            return true;
        }

        void add_token(Lexer *l, syntax::TokenKind kind) {
            l->tokens.push_back(syntax::Token{
                .kind = kind,
                .lexeme = l->source.substr(l->start, l->current - l->start),
                .span = current_span(l),
            });
        }

        void add_token_string(Lexer *l, syntax::TokenKind kind, std::string_view value) {
            l->tokens.push_back(syntax::Token{
                .kind = kind,
                .lexeme = value,
                .span = current_span(l),
            });
        }

        template<typename... Args>
        void error_here(Lexer *l, std::format_string<Args...> fmt, Args &&... args) {
            diagnostics::report_error(
                l->diag,
                current_span(l),
                fmt,
                std::forward<Args>(args)...);
        }

        // Assumes peek(l) is '\r' or '\n'.
        void scan_newline(Lexer *l) noexcept {
            if (peek(l) == '\r') {
                advance(l);
                if (peek(l) == '\n') advance(l);
            } else {
                advance(l);
            }
            ++l->line;
            l->column = 1;
        }

        void skip_whitespace(Lexer *l) noexcept {
            while (!is_at_end(l)) {
                char c = peek(l);
                if (c != ' ' && c != '\t') return;
                advance(l);
            }
        }

        void skip_line_comment(Lexer *l) noexcept {
            while (!is_at_end(l)) {
                char c = peek(l);
                if (c == '\n' || c == '\r') return;
                advance(l);
            }
        }

        void number(Lexer *l) {
            while (is_digit(peek(l))) advance(l);
            if (peek(l) == '.' && is_digit(peek_next(l))) {
                advance(l);
                while (is_digit(peek(l))) advance(l);
                add_token(l, syntax::TokenKind::Float);
            } else {
                add_token(l, syntax::TokenKind::Int);
            }
        }

        void string_literal(Lexer *l) {
            // Opening '"' already consumed by caller.
            while (!is_at_end(l)) {
                char c = peek(l);
                if (c == '"') break;
                if (c == '\n' || c == '\r') {
                    scan_newline(l);
                } else {
                    advance(l);
                }
            }
            if (is_at_end(l)) {
                error_here(l, "Unterminated string literal");
                add_token_string(l, syntax::TokenKind::Str, l->source.substr(l->start + 1));
                return;
            }
            advance(l); // closing '"'
            add_token_string(l, syntax::TokenKind::Str,
                             l->source.substr(l->start + 1, l->current - l->start - 2));
        }

        void identifier(Lexer *l) {
            while (is_ident_char(peek(l))) advance(l);
            auto text = l->source.substr(l->start, l->current - l->start);
            if (auto kind = syntax::lookup_keyword(text); kind) {
                add_token(l, *kind);
            } else {
                add_token(l, syntax::TokenKind::Ident);
            }
        }
    }

    Lexer make_lexer(std::string_view source, diagnostics::Engine *diag, std::pmr::memory_resource *mr) {
        return Lexer{
            .source = source,
            .tokens = std::pmr::vector<syntax::Token>{mr},
            .diag = diag,
        };
    }

    std::span<const syntax::Token> tokenize(Lexer *l) {
        while (!is_at_end(l)) {
            char c0 = peek(l);
            if (c0 == ' ' || c0 == '\t') {
                skip_whitespace(l);
                continue;
            }
            if (c0 == '\r' || c0 == '\n') {
                scan_newline(l);
                continue;
            }

            l->start = l->current;
            l->token_line = l->line;
            l->token_column = l->column;

            switch (char c = advance(l)) {
                case '(': add_token(l, syntax::TokenKind::LParen);
                    break;
                case ')': add_token(l, syntax::TokenKind::RParen);
                    break;
                case '{': add_token(l, syntax::TokenKind::LBrace);
                    break;
                case '}': add_token(l, syntax::TokenKind::RBrace);
                    break;
                case '[': add_token(l, syntax::TokenKind::LBrack);
                    break;
                case ']': add_token(l, syntax::TokenKind::RBrack);
                    break;
                case ':': add_token(l, syntax::TokenKind::Colon);
                    break;
                case '.': add_token(l, syntax::TokenKind::Dot);
                    break;
                case ';': add_token(l, syntax::TokenKind::SemiColon);
                    break;
                case ',': add_token(l, syntax::TokenKind::Comma);
                    break;
                case '?': add_token(l, syntax::TokenKind::Question);
                    break;

                case '=':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::EqEq
                                     : syntax::TokenKind::Eq);
                    break;

                case '-':
                    if (match(l, '>')) add_token(l, syntax::TokenKind::Arrow);
                    else if (match(l, '=')) add_token(l, syntax::TokenKind::MinusEq);
                    else add_token(l, syntax::TokenKind::Minus);
                    break;

                case '+':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::PlusEq
                                     : syntax::TokenKind::Plus);
                    break;
                case '*':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::StarEq
                                     : syntax::TokenKind::Star);
                    break;

                case '/':
                    if (match(l, '/')) {
                        skip_line_comment(l);
                        continue;
                    }
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::SlashEq
                                     : syntax::TokenKind::Slash);
                    break;

                case '%':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::ModEq
                                     : syntax::TokenKind::Mod);
                    break;

                case '|':
                    if (match(l, '|')) add_token(l, syntax::TokenKind::Or);
                    else error_here(l, "unexpected character: '|'. did you mean '||'?");
                    break;

                case '&':
                    if (match(l, '&')) add_token(l, syntax::TokenKind::And);
                    else error_here(l, "unexpected character: '&'. did you mean '&&'?");
                    break;

                case '!':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::Neq
                                     : syntax::TokenKind::Not);
                    break;
                case '>':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::Gte
                                     : syntax::TokenKind::Gt);
                    break;
                case '<':
                    add_token(l, match(l, '=')
                                     ? syntax::TokenKind::Lte
                                     : syntax::TokenKind::Lt);
                    break;

                default:
                    if (is_alpha(c)) identifier(l);
                    else if (is_digit(c)) number(l);
                    else if (c == '"') string_literal(l);
                    else error_here(l, "unexpected character: '{}'", c);
                    break;
            }
        }
        l->start = l->current;
        l->token_line = l->line;
        l->token_column = l->column;
        l->tokens.push_back(syntax::Token{
            .kind = syntax::TokenKind::Eof,
            .lexeme = {},
            .span = current_span(l)
        });
        return std::span<const syntax::Token>(l->tokens);
    }
}
