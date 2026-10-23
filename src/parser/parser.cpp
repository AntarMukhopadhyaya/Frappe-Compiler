//
// Created by antar on 19/10/26.
//

#include <cassert>
#include <frappe/parser/parser.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <string_view>
#include <utility>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/node.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>
#include <frappe/parser/operators.hpp>
#include <frappe/source/span.hpp>

namespace frappe::parser {
    namespace {
        using TK = syntax::TokenKind;

        [[nodiscard]] bool is_at_end(const Parser *p) noexcept {
            return p->current >= p->tokens.size()
                   || p->tokens[p->current].kind == TK::Eof;
        }

        [[nodiscard]] const syntax::Token &peek(const Parser *p) noexcept {
            if (p->current >= p->tokens.size()) return p->tokens.back();
            return p->tokens[p->current];
        }

        [[nodiscard]] const syntax::Token &peek_at(const Parser *p, std::size_t offset) noexcept {
            std::size_t idx = p->current + offset;
            if (idx >= p->tokens.size()) idx = p->tokens.size() - 1;
            return p->tokens[idx];
        }

        [[nodiscard]] const syntax::Token &previous(const Parser *p) noexcept {
            assert(p->current > 0);
            return p->tokens[p->current - 1];
        }

        const syntax::Token &advance(Parser *p) noexcept {
            if (!is_at_end(p)) ++p->current;
            return previous(p);
        }

        [[nodiscard]] bool check(const Parser *p, TK k) noexcept {
            return !is_at_end(p) && peek(p).kind == k;
        }

        bool match(Parser *p, TK k) noexcept {
            if (check(p, k)) {
                advance(p);
                return true;
            }
            return false;
        }

        syntax::Token consume(Parser *p, TK k, std::string_view msg) {
            if (check(p, k)) return advance(p);
            auto tok = peek(p);
            diagnostics::error(p->diag, tok.span, msg);
            return tok;
        }

        template<typename... Args>
        void error_here(Parser *p, std::format_string<Args...> fmt, Args &&... args) {
            diagnostics::report_error(p->diag, peek(p).span, fmt, std::forward<Args>(args)...);
        }

        void recover_decl(Parser *p) {
            while (!is_at_end(p)) {
                auto k = peek(p).kind;
                if (k == TK::Fn || k == TK::Class ||
                    k == TK::Enum || k == TK::Interface ||
                    k == TK::Import || k == TK::Let ||
                    k == TK::Var || k == TK::Eof) {
                    return;
                }
                advance(p);
            }
        }

        void recover_stmt(Parser *p) {
            while (!is_at_end(p)) {
                auto k = peek(p).kind;
                if (k == TK::SemiColon) {
                    advance(p);
                    return;
                }
                if (k == TK::RBrace || k == TK::Eof) return;
                advance(p);
            }
        }

        void ensure_progress(Parser *p, std::size_t before) {
            if (p->current == before && !is_at_end(p)) advance(p);
        }


        template<typename T, typename... Args>
        [[nodiscard]] T *alloc(Parser *p, Args &&... args) {
            void *mem = p->mr->allocate(sizeof(T), alignof(T));
            return ::new(mem) T{std::forward<Args>(args)...};
        }

        template<typename T>
        [[nodiscard]] T *alloc_obj(Parser *p, T obj) {
            void *mem = p->mr->allocate(sizeof(T), alignof(T));
            return ::new(mem) T{std::move(obj)};
        }

        [[nodiscard]] inline ast::Name to_name(const syntax::Token &tok) noexcept {
            return ast::Name{tok.lexeme, tok.span};
        }


        struct Modifiers {
            ast::Visibility visibility = ast::Visibility::Default;
            bool is_static = false;
            bool is_abstract = false;
            bool is_final = false;
            bool is_override = false;
        };

        [[nodiscard]] Modifiers parse_modifiers(Parser *p) {
            Modifiers m;
            while (!is_at_end(p)) {
                auto k = peek(p).kind;
                if (k == TK::Private) {
                    advance(p);
                    m.visibility = ast::Visibility::Private;
                    continue;
                }
                if (k == TK::Public) {
                    advance(p);
                    m.visibility = ast::Visibility::Public;
                    continue;
                }
                if (k == TK::Protected) {
                    advance(p);
                    m.visibility = ast::Visibility::Protected;
                    continue;
                }
                if (k == TK::Static) {
                    advance(p);
                    m.is_static = true;
                    continue;
                }
                if (k == TK::Final) {
                    advance(p);
                    m.is_final = true;
                    continue;
                }
                if (k == TK::Abstract) {
                    advance(p);
                    m.is_abstract = true;
                    continue;
                }
                if (k == TK::Override) {
                    advance(p);
                    m.is_override = true;
                    continue;
                }
                break;
            }
            return m;
        }


        [[nodiscard]] ast::Decl *parse_decl(Parser *p);

        [[nodiscard]] ast::ImportDecl *parse_import_decl(Parser *p);

        [[nodiscard]] ast::FnDecl *parse_fn_decl(Parser *p, Modifiers m);

        [[nodiscard]] ast::ClassDecl *parse_class_decl(Parser *p, Modifiers m);

        [[nodiscard]] ast::FieldDecl *parse_field_decl(Parser *p, Modifiers m);

        [[nodiscard]] ast::InterfaceDecl *parse_interface_decl(Parser *p, Modifiers m);

        [[nodiscard]] ast::EnumDecl *parse_enum_decl(Parser *p, Modifiers m);

        [[nodiscard]] ast::VarDecl *parse_var_decl(Parser *p, Modifiers m);

        [[nodiscard]] std::pmr::vector<ast::TypeParam *> parse_type_params(Parser *p);

        [[nodiscard]] std::pmr::vector<ast::Param *> parse_param_list(Parser *p);

        [[nodiscard]] ast::Stmt *parse_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_var_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_if_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_while_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_for_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_for_init(Parser *p);

        [[nodiscard]] ast::Stmt *parse_switch_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_break_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_continue_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_return_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_defer_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_panic_stmt(Parser *p);

        [[nodiscard]] ast::BlockStmt *parse_block_stmt(Parser *p);

        [[nodiscard]] ast::Stmt *parse_expr_stmt(Parser *p);

        [[nodiscard]] ast::Expr *parse_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_assign_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_lor_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_land_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_eq_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_comp_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_add_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_mul_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_unary_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_postfix_expr(Parser *p);

        [[nodiscard]] ast::Expr *parse_primary_expr(Parser *p);

        [[nodiscard]] std::pmr::vector<ast::Expr *> parse_call_args(Parser *p);

        [[nodiscard]] ast::Type *parse_type(Parser *p);

        [[nodiscard]] ast::Type *parse_func_type(Parser *p);

        [[nodiscard]] ast::Type *parse_postfix_type(Parser *p);

        [[nodiscard]] ast::Type *parse_applied_type(Parser *p);

        [[nodiscard]] ast::Type *parse_primary_type(Parser *p);

        [[nodiscard]] ast::Decl *parse_decl(Parser *p) {
            auto m = parse_modifiers(p);
            auto tok = peek(p);
            if (tok.kind == TK::Import) return parse_import_decl(p);
            if (tok.kind == TK::Fn) return parse_fn_decl(p, m);
            if (tok.kind == TK::Class) return parse_class_decl(p, m);
            if (tok.kind == TK::Interface) return parse_interface_decl(p, m);
            if (tok.kind == TK::Enum) return parse_enum_decl(p, m);
            if (tok.kind == TK::Let) return parse_var_decl(p, m);
            if (tok.kind == TK::Var) return parse_var_decl(p, m);

            error_here(p, "expected top-level declaration");
            return nullptr;
        }

        [[nodiscard]] ast::ImportDecl *parse_import_decl(Parser *p) {
            auto import_tok = advance(p);

            auto path_tok = consume(p, TK::Str, "expected import path string");
            if (path_tok.kind != TK::Str) return nullptr;

            ast::Name alias{};
            bool has_alias = false;
            if (match(p, TK::As)) {
                auto a = consume(p, TK::Ident, "expected alias after 'as'");
                if (a.kind != TK::Ident) return nullptr;
                alias = to_name(a);
                has_alias = true;
            }

            auto semi = consume(p, TK::SemiColon, "expected ';' after import declaration");
            if (semi.kind != TK::SemiColon) return nullptr;

            auto span = source::join(import_tok.span, semi.span);
            return alloc<ast::ImportDecl>(p,
                                          ast::Decl{ast::DeclKind::Import, span},
                                          to_name(path_tok),
                                          alias,
                                          has_alias);
        }

        [[nodiscard]] std::pmr::vector<ast::TypeParam *> parse_type_params(Parser *p) {
            std::pmr::vector<ast::TypeParam *> params{p->mr};
            if (!match(p, TK::Lt)) return params;

            if (!check(p, TK::Gt)) {
                while (true) {
                    std::size_t before = p->current;
                    auto name_tok = consume(p, TK::Ident, "expected type parameter name");
                    if (name_tok.kind != TK::Ident) return params;

                    params.push_back(alloc_obj(p, ast::TypeParam{
                                                   .span = name_tok.span,
                                                   .name = to_name(name_tok),
                                                   .bound = nullptr,
                                               }));

                    if (!match(p, TK::Comma)) break;
                    ensure_progress(p, before);
                }
            }

            auto gt = consume(p, TK::Gt, "expected '>' to close type parameters");
            if (gt.kind != TK::Gt) return params;
            return params;
        }

        [[nodiscard]] std::pmr::vector<ast::Param *> parse_param_list(Parser *p) {
            std::pmr::vector<ast::Param *> params{p->mr};
            if (check(p, TK::RParen)) return params;

            while (true) {
                std::size_t before = p->current;
                auto pt = consume(p, TK::Ident, "expected parameter name");
                if (pt.kind != TK::Ident) return params;

                auto colon = consume(p, TK::Colon, "expected ':' after parameter name");
                if (colon.kind != TK::Colon) return params;

                auto *ty = parse_type(p);
                if (!ty) return params;

                auto span = source::join(pt.span, ty->span);
                params.push_back(alloc_obj(p, ast::Param{
                                               .span = span,
                                               .name = to_name(pt),
                                               .type_ = ty,
                                           }));

                if (!match(p, TK::Comma)) break;
                ensure_progress(p, before);
            }
            return params;
        }

        [[nodiscard]] ast::FnDecl *parse_fn_decl(Parser *p, Modifiers m) {
            auto fn_tok = advance(p);

            auto name_tok = consume(p, TK::Ident, "expected function name");
            if (name_tok.kind != TK::Ident) return nullptr;

            auto tp_before = p->diag->error_count;
            auto type_params = parse_type_params(p);
            if (p->diag->error_count > tp_before) return nullptr;

            auto lparen = consume(p, TK::LParen, "expected '(' after function name");
            if (lparen.kind != TK::LParen) return nullptr;

            auto pl_before = p->diag->error_count;
            auto params = parse_param_list(p);
            if (p->diag->error_count > pl_before) return nullptr;

            auto rparen = consume(p, TK::RParen, "expected ')' after parameters");
            if (rparen.kind != TK::RParen) return nullptr;

            ast::Type *ret = nullptr;
            if (match(p, TK::Arrow)) {
                ret = parse_type(p);
                if (!ret) return nullptr;
            }

            ast::BlockStmt *body = nullptr;
            if (check(p, TK::LBrace)) {
                body = parse_block_stmt(p);
                if (!body) return nullptr;
            } else {
                auto semi = consume(p, TK::SemiColon,
                                    "expected ';' or '{' after function signature");
                if (semi.kind != TK::SemiColon) return nullptr;
            }

            auto span = source::join(fn_tok.span, previous(p).span);
            return alloc<ast::FnDecl>(p,
                                      ast::Decl{ast::DeclKind::Fn, span},
                                      to_name(name_tok),
                                      std::move(type_params),
                                      std::move(params),
                                      ret,
                                      body,
                                      m.visibility,
                                      m.is_static,
                                      m.is_final,
                                      m.is_override,
                                      m.is_abstract,
                                      false, // is_constructor — set in parse_class_decl
                                      false); // is_destructor  — set in parse_class_decl
        }

        [[nodiscard]] ast::FieldDecl *parse_field_decl(Parser *p, Modifiers m) {
            auto name_tok = consume(p, TK::Ident, "expected field name");
            if (name_tok.kind != TK::Ident) return nullptr;

            auto colon = consume(p, TK::Colon, "expected ':' after field name");
            if (colon.kind != TK::Colon) return nullptr;

            auto *ty = parse_type(p);
            if (!ty) return nullptr;

            ast::Expr *init = nullptr;
            if (match(p, TK::Eq)) {
                init = parse_expr(p);
                if (!init) return nullptr;
            }

            auto semi = consume(p, TK::SemiColon, "expected ';' after field declaration");
            if (semi.kind != TK::SemiColon) return nullptr;

            auto span = source::join(name_tok.span, semi.span);
            return alloc_obj(p, ast::FieldDecl{
                                 .span = span,
                                 .name = to_name(name_tok),
                                 .type_ = ty,
                                 .init = init,
                                 .visibility = m.visibility,
                                 .is_static = m.is_static,
                                 .is_final = m.is_final,
                             });
        }

        [[nodiscard]] ast::ClassDecl *parse_class_decl(Parser *p, Modifiers m) {
            auto class_tok = advance(p);

            auto name_tok = consume(p, TK::Ident, "expected class name");
            if (name_tok.kind != TK::Ident) return nullptr;

            auto tp_before = p->diag->error_count;
            auto type_params = parse_type_params(p);
            if (p->diag->error_count > tp_before) return nullptr;

            ast::Type *base = nullptr;
            std::pmr::vector<ast::Type *> interfaces{p->mr};

            if (match(p, TK::Extends)) {
                base = parse_type(p);
                if (!base) return nullptr;
            }
            if (match(p, TK::Implements)) {
                while (true) {
                    auto before = p->current;
                    auto *iface = parse_type(p);
                    if (!iface) return nullptr;
                    interfaces.push_back(iface);
                    if (!match(p, TK::Comma)) break;
                    ensure_progress(p, before);
                }
            }

            auto lbrace = consume(p, TK::LBrace, "expected '{' to start class body");
            if (lbrace.kind != TK::LBrace) return nullptr;

            std::pmr::vector<ast::FieldDecl *> fields{p->mr};
            std::pmr::vector<ast::FnDecl *> methods{p->mr};

            while (!check(p, TK::RBrace) && !is_at_end(p)) {
                auto before = p->current;
                auto mem_mods = parse_modifiers(p);

                if (check(p, TK::Fn)) {
                    auto *fn = parse_fn_decl(p, mem_mods);
                    if (!fn) {
                        recover_stmt(p);
                        ensure_progress(p, before);
                        continue;
                    }
                    methods.push_back(fn);
                } else if (check(p, TK::Ident)) {
                    if (auto *field = parse_field_decl(p, mem_mods)) fields.push_back(field);
                } else {
                    diagnostics::error(p->diag, peek(p).span,
                                       "expected field or method in class body");
                    recover_stmt(p);
                }
                ensure_progress(p, before);
            }

            auto rbrace = consume(p, TK::RBrace, "expected '}' to close class body");
            if (rbrace.kind != TK::RBrace) return nullptr;

            for (auto *m_: methods) {
                if (m_->name.text == "init") m_->is_constructor = true;
                else if (m_->name.text == "deinit") m_->is_destructor = true;
            }

            auto span = source::join(class_tok.span, rbrace.span);
            return alloc<ast::ClassDecl>(p,
                                         ast::Decl{ast::DeclKind::Class, span},
                                         to_name(name_tok),
                                         std::move(type_params),
                                         base,
                                         std::move(interfaces),
                                         std::move(fields),
                                         std::move(methods),
                                         m.visibility,
                                         m.is_final,
                                         m.is_abstract);
        }

        [[nodiscard]] ast::InterfaceDecl *parse_interface_decl(Parser *p, Modifiers m) {
            auto iface_tok = advance(p);

            auto name_tok = consume(p, TK::Ident, "expected interface name");
            if (name_tok.kind != TK::Ident) return nullptr;

            auto tp_before = p->diag->error_count;
            auto type_params = parse_type_params(p);
            if (p->diag->error_count > tp_before) return nullptr;

            std::pmr::vector<ast::Type *> bases{p->mr};
            if (match(p, TK::Extends)) {
                while (true) {
                    auto before = p->current;
                    auto *b = parse_type(p);
                    if (!b) return nullptr;
                    bases.push_back(b);
                    if (!match(p, TK::Comma)) break;
                    ensure_progress(p, before);
                }
            }

            auto lbrace = consume(p, TK::LBrace, "expected '{' to start interface body");
            if (lbrace.kind != TK::LBrace) return nullptr;

            std::pmr::vector<ast::FnDecl *> methods{p->mr};
            while (!check(p, TK::RBrace) && !is_at_end(p)) {
                auto before = p->current;
                auto mem_mods = parse_modifiers(p);
                if (!check(p, TK::Fn)) {
                    error_here(p, "expected method signature in interface");
                    recover_stmt(p);
                    ensure_progress(p, before);
                    continue;
                }
                if (auto *fn = parse_fn_decl(p, mem_mods)) methods.push_back(fn);
                ensure_progress(p, before);
            }

            auto rbrace = consume(p, TK::RBrace, "expected '}' to close interface body");
            if (rbrace.kind != TK::RBrace) return nullptr;

            auto span = source::join(iface_tok.span, rbrace.span);
            return alloc<ast::InterfaceDecl>(p,
                                             ast::Decl{ast::DeclKind::Interface, span},
                                             to_name(name_tok),
                                             std::move(type_params),
                                             std::move(bases),
                                             std::move(methods),
                                             m.visibility);
        }

        [[nodiscard]] ast::EnumDecl *parse_enum_decl(Parser *p, Modifiers m) {
            auto enum_tok = advance(p);

            auto name_tok = consume(p, TK::Ident, "expected enum name");
            if (name_tok.kind != TK::Ident) return nullptr;

            auto lbrace = consume(p, TK::LBrace, "expected '{' to start enum body");
            if (lbrace.kind != TK::LBrace) return nullptr;

            std::pmr::vector<ast::EnumVariant *> variants{p->mr};
            while (!check(p, TK::RBrace) && !is_at_end(p)) {
                auto before = p->current;
                auto vt = consume(p, TK::Ident, "expected enum variant name");
                if (vt.kind != TK::Ident) return nullptr;

                ast::Expr *value = nullptr;
                if (match(p, TK::Eq)) {
                    value = parse_expr(p);
                    if (!value) return nullptr;
                }

                auto end = vt.span.end;
                if (value) end = value->span.end;

                variants.push_back(alloc_obj(p, ast::EnumVariant{
                                                 .span = source::Span{vt.span.start, end},
                                                 .name = to_name(vt),
                                                 .value = value,
                                             }));

                if (!match(p, TK::Comma)) break;
                ensure_progress(p, before);
            }

            auto rbrace = consume(p, TK::RBrace, "expected '}' to close enum body");
            if (rbrace.kind != TK::RBrace) return nullptr;

            auto span = source::join(enum_tok.span, rbrace.span);
            return alloc<ast::EnumDecl>(p,
                                        ast::Decl{ast::DeclKind::Enum, span},
                                        to_name(name_tok),
                                        std::move(variants),
                                        m.visibility);
        }

        [[nodiscard]] ast::VarDecl *parse_var_decl(Parser *p, Modifiers m) {
            auto kw_tok = advance(p);
            bool is_mutable = kw_tok.kind == TK::Var;

            auto name_tok = consume(p, TK::Ident, "expected variable name");
            if (name_tok.kind != TK::Ident) return nullptr;

            ast::Type *ty = nullptr;
            if (match(p, TK::Colon)) {
                ty = parse_type(p);
                if (!ty) return nullptr;
            }
            ast::Expr *init = nullptr;
            if (match(p, TK::Eq)) {
                init = parse_expr(p);
                if (!init) return nullptr;
            }
            if (!ty && !init) {
                diagnostics::error(p->diag, name_tok.span,
                                   "variable declaration needs a type or an initializer");
                return nullptr;
            }

            auto semi = consume(p, TK::SemiColon, "expected ';' after variable declaration");
            if (semi.kind != TK::SemiColon) return nullptr;

            auto span = source::join(kw_tok.span, semi.span);
            return alloc<ast::VarDecl>(p,
                                       ast::Decl{ast::DeclKind::Var, span},
                                       to_name(name_tok),
                                       ty,
                                       init,
                                       m.visibility,
                                       is_mutable);
        }

        [[nodiscard]] ast::Stmt *parse_stmt(Parser *p) {
            auto k = peek(p).kind;
            if (k == TK::Let || k == TK::Var) return parse_var_stmt(p);
            if (k == TK::If) return parse_if_stmt(p);
            if (k == TK::While) return parse_while_stmt(p);
            if (k == TK::For) return parse_for_stmt(p);
            if (k == TK::Switch) return parse_switch_stmt(p);
            if (k == TK::Break) return parse_break_stmt(p);
            if (k == TK::Continue) return parse_continue_stmt(p);
            if (k == TK::Return) return parse_return_stmt(p);
            if (k == TK::Defer) return parse_defer_stmt(p);
            if (k == TK::Panic) return parse_panic_stmt(p);
            if (k == TK::LBrace) return parse_block_stmt(p);
            return parse_expr_stmt(p);
        }

        [[nodiscard]] ast::Stmt *parse_var_stmt(Parser *p) {
            auto kw_tok = advance(p);
            bool is_mutable = kw_tok.kind == TK::Var;

            auto name_tok = consume(p, TK::Ident, "expected variable name");
            if (name_tok.kind != TK::Ident) return nullptr;

            ast::Type *ty = nullptr;
            if (match(p, TK::Colon)) {
                ty = parse_type(p);
                if (!ty) return nullptr;
            }
            ast::Expr *init = nullptr;
            if (match(p, TK::Eq)) {
                init = parse_expr(p);
                if (!init) return nullptr;
            }
            if (!ty && !init) {
                diagnostics::error(p->diag, name_tok.span,
                                   "variable declaration needs a type or an initializer");
                return nullptr;
            }

            auto semi = consume(p, TK::SemiColon, "expected ';' after variable declaration");
            if (semi.kind != TK::SemiColon) return nullptr;

            auto span = source::join(kw_tok.span, semi.span);
            return alloc<ast::VarStmt>(p,
                                       ast::Stmt{ast::StmtKind::Var, span},
                                       to_name(name_tok),
                                       ty,
                                       init,
                                       is_mutable);
        }

        [[nodiscard]] ast::Stmt *parse_if_stmt(Parser *p) {
            auto if_tok = advance(p);

            auto lparen = consume(p, TK::LParen, "expected '(' after 'if'");
            if (lparen.kind != TK::LParen) return nullptr;
            auto *cond = parse_expr(p);
            if (!cond) return nullptr;
            auto rparen = consume(p, TK::RParen, "expected ')' after if condition");
            if (rparen.kind != TK::RParen) return nullptr;

            auto *then_blk = parse_block_stmt(p);
            if (!then_blk) return nullptr;

            ast::Stmt *else_branch = nullptr;
            if (match(p, TK::Else)) {
                if (check(p, TK::If)) {
                    else_branch = parse_if_stmt(p);
                } else {
                    else_branch = parse_block_stmt(p);
                }
                if (!else_branch) return nullptr;
            }

            auto end = then_blk->span.end;
            if (else_branch) end = else_branch->span.end;

            return alloc<ast::IfStmt>(p,
                                      ast::Stmt{ast::StmtKind::If, source::Span{if_tok.span.start, end}},
                                      cond, then_blk, else_branch);
        }

        [[nodiscard]] ast::Stmt *parse_while_stmt(Parser *p) {
            auto while_tok = advance(p);
            auto lparen = consume(p, TK::LParen, "expected '(' after 'while'");
            if (lparen.kind != TK::LParen) return nullptr;
            auto *cond = parse_expr(p);
            if (!cond) return nullptr;
            auto rparen = consume(p, TK::RParen, "expected ')' after while condition");
            if (rparen.kind != TK::RParen) return nullptr;
            auto *body = parse_block_stmt(p);
            if (!body) return nullptr;

            auto span = source::join(while_tok.span, body->span);
            return alloc<ast::WhileStmt>(p,
                                         ast::Stmt{ast::StmtKind::While, span}, cond, body);
        }

        [[nodiscard]] ast::Stmt *parse_for_init(Parser *p) {
            auto k = peek(p).kind;
            if (k == TK::Let || k == TK::Var) {
                auto kw_tok = advance(p);
                bool is_mutable = kw_tok.kind == TK::Var;

                auto name_tok = consume(p, TK::Ident, "expected variable name");
                if (name_tok.kind != TK::Ident) return nullptr;

                ast::Type *ty = nullptr;
                if (match(p, TK::Colon)) {
                    ty = parse_type(p);
                    if (!ty) return nullptr;
                }
                ast::Expr *init = nullptr;
                if (match(p, TK::Eq)) {
                    init = parse_expr(p);
                    if (!init) return nullptr;
                }

                auto end = name_tok.span.end;
                if (init) end = init->span.end;
                else if (ty) end = ty->span.end;

                return alloc<ast::VarStmt>(p,
                                           ast::Stmt{ast::StmtKind::Var, source::Span{kw_tok.span.start, end}},
                                           to_name(name_tok), ty, init, is_mutable);
            }
            auto *e = parse_expr(p);
            if (!e) return nullptr;
            return alloc<ast::ExprStmt>(p,
                                        ast::Stmt{ast::StmtKind::Expr, e->span}, e);
        }

        [[nodiscard]] ast::Stmt *parse_for_stmt(Parser *p) {
            auto for_tok = advance(p);

            // for-in form: `for x in expr { ... }`
            if (check(p, TK::Ident) && peek_at(p, 1).kind == TK::In) {
                auto var_tok = advance(p);
                advance(p); // 'in'
                auto *iter = parse_expr(p);
                if (!iter) return nullptr;
                auto *body = parse_block_stmt(p);
                if (!body) return nullptr;
                return alloc<ast::ForInStmt>(p,
                                             ast::Stmt{ast::StmtKind::ForIn, source::join(for_tok.span, body->span)},
                                             to_name(var_tok), iter, body);
            }

            // C-style: `for (init; cond; update) { ... }`
            auto lparen = consume(p, TK::LParen, "expected '(' or 'x in' after 'for'");
            if (lparen.kind != TK::LParen) return nullptr;

            ast::Stmt *init = nullptr;
            if (!check(p, TK::SemiColon)) {
                init = parse_for_init(p);
                if (!init) return nullptr;
            }
            auto s1 = consume(p, TK::SemiColon, "expected ';' after for-init");
            if (s1.kind != TK::SemiColon) return nullptr;

            ast::Expr *cond = nullptr;
            if (!check(p, TK::SemiColon)) {
                cond = parse_expr(p);
                if (!cond) return nullptr;
            }
            auto s2 = consume(p, TK::SemiColon, "expected ';' after for-condition");
            if (s2.kind != TK::SemiColon) return nullptr;

            ast::Expr *update = nullptr;
            if (!check(p, TK::RParen)) {
                update = parse_expr(p);
                if (!update) return nullptr;
            }
            auto rparen = consume(p, TK::RParen, "expected ')' after for clause");
            if (rparen.kind != TK::RParen) return nullptr;

            auto *body = parse_block_stmt(p);
            if (!body) return nullptr;

            return alloc<ast::ForStmt>(p,
                                       ast::Stmt{ast::StmtKind::For, source::join(for_tok.span, body->span)},
                                       init, cond, update, body);
        }

        [[nodiscard]] ast::Stmt *parse_switch_stmt(Parser *p) {
            auto switch_tok = advance(p);
            auto lparen = consume(p, TK::LParen, "expected '(' after 'switch'");
            if (lparen.kind != TK::LParen) return nullptr;
            auto *subject = parse_expr(p);
            if (!subject) return nullptr;
            auto rparen = consume(p, TK::RParen, "expected ')' after switch subject");
            if (rparen.kind != TK::RParen) return nullptr;

            auto lbrace = consume(p, TK::LBrace, "expected '{' to start switch body");
            if (lbrace.kind != TK::LBrace) return nullptr;

            std::pmr::vector<ast::CaseClause *> cases{p->mr};
            std::pmr::vector<ast::Stmt *> default_body{p->mr};
            bool has_default = false;

            while (!check(p, TK::RBrace) && !is_at_end(p)) {
                auto before = p->current;
                if (match(p, TK::Case)) {
                    std::pmr::vector<ast::Expr *> values{p->mr};
                    while (true) {
                        auto *e = parse_expr(p);
                        if (!e) return nullptr;
                        values.push_back(e);
                        if (!match(p, TK::Comma)) break;
                    }
                    auto colon = consume(p, TK::Colon, "expected ':' after case values");
                    if (colon.kind != TK::Colon) return nullptr;

                    std::pmr::vector<ast::Stmt *> body{p->mr};
                    while (!check(p, TK::Case) && !check(p, TK::Default)
                           && !check(p, TK::RBrace) && !is_at_end(p)) {
                        auto inner = p->current;
                        if (auto *s = parse_stmt(p)) body.push_back(s);
                        else recover_stmt(p);
                        ensure_progress(p, inner);
                    }
                    auto end = colon.span.end;
                    if (!body.empty()) end = body.back()->span.end;

                    cases.push_back(alloc_obj(p, ast::CaseClause{
                                                  .span = source::Span{colon.span.start, end},
                                                  .values = std::move(values),
                                                  .body = std::move(body),
                                              }));
                } else if (match(p, TK::Default)) {
                    if (has_default) {
                        diagnostics::error(p->diag, previous(p).span,
                                           "duplicate 'default' clause");
                    }
                    has_default = true;
                    auto colon = consume(p, TK::Colon, "expected ':' after 'default'");
                    if (colon.kind != TK::Colon) return nullptr;
                    while (!check(p, TK::Case) && !check(p, TK::Default)
                           && !check(p, TK::RBrace) && !is_at_end(p)) {
                        auto inner = p->current;
                        if (auto *s = parse_stmt(p)) default_body.push_back(s);
                        else recover_stmt(p);
                        ensure_progress(p, inner);
                    }
                } else {
                    error_here(p, "expected 'case' or 'default' in switch body");

                    recover_stmt(p);
                }
                ensure_progress(p, before);
            }

            auto rbrace = consume(p, TK::RBrace, "expected '}' to close switch body");
            if (rbrace.kind != TK::RBrace) return nullptr;

            auto span = source::join(switch_tok.span, rbrace.span);
            return alloc<ast::SwitchStmt>(p,
                                          ast::Stmt{ast::StmtKind::Switch, span},
                                          subject, std::move(cases), std::move(default_body));
        }

        [[nodiscard]] ast::Stmt *parse_break_stmt(Parser *p) {
            auto tok = advance(p);
            auto semi = consume(p, TK::SemiColon, "expected ';' after 'break'");
            if (semi.kind != TK::SemiColon) return nullptr;
            return alloc<ast::BreakStmt>(p,
                                         ast::Stmt{ast::StmtKind::Break, source::join(tok.span, semi.span)});
        }

        [[nodiscard]] ast::Stmt *parse_continue_stmt(Parser *p) {
            auto tok = advance(p);
            auto semi = consume(p, TK::SemiColon, "expected ';' after 'continue'");
            if (semi.kind != TK::SemiColon) return nullptr;
            return alloc<ast::ContinueStmt>(p,
                                            ast::Stmt{ast::StmtKind::Continue, source::join(tok.span, semi.span)});
        }

        [[nodiscard]] ast::Stmt *parse_return_stmt(Parser *p) {
            auto ret_tok = advance(p);
            ast::Expr *value = nullptr;
            if (!check(p, TK::SemiColon)) {
                value = parse_expr(p);
                if (!value) return nullptr;
            }
            auto semi = consume(p, TK::SemiColon, "expected ';' after return");
            if (semi.kind != TK::SemiColon) return nullptr;

            auto span = source::join(ret_tok.span, semi.span);
            return alloc<ast::ReturnStmt>(p,
                                          ast::Stmt{ast::StmtKind::Return, span}, value);
        }

        [[nodiscard]] ast::Stmt *parse_defer_stmt(Parser *p) {
            auto defer_tok = advance(p);

            ast::Stmt *body = nullptr;

            if (check(p, TK::LBrace)) {
                // `defer { ... }` — block form, no trailing semicolon.
                // parse_block_stmt returns BlockStmt*; BlockStmt : Stmt → implicit upcast.
                body = parse_block_stmt(p);
                if (!body) return nullptr;
            } else {
                // `defer expr;` — expression form, trailing semicolon required.
                auto *expr = parse_expr(p);
                if (!expr) return nullptr;

                auto semi = consume(p, TK::SemiColon,
                                    "expected ';' after deferred expression");
                if (semi.kind != TK::SemiColon) return nullptr;

                // Wrap in ExprStmt so DeferStmt.body (Stmt*) can hold it.
                // alloc<ExprStmt> returns ExprStmt*; ExprStmt : Stmt → implicit upcast.
                auto span = source::join(expr->span, semi.span);
                body = alloc<ast::ExprStmt>(p,
                                            ast::Stmt{ast::StmtKind::Expr, span},
                                            expr);
            }

            auto span = source::join(defer_tok.span, body->span);
            return alloc<ast::DeferStmt>(p,
                                         ast::Stmt{ast::StmtKind::Defer, span},
                                         body);
        }

        [[nodiscard]] ast::Stmt *parse_panic_stmt(Parser *p) {
            auto panic_tok = advance(p);

            auto lparen = consume(p, TK::LParen, "expected '(' after 'panic'");
            if (lparen.kind != TK::LParen) return nullptr;

            auto *msg = parse_expr(p);
            if (!msg) return nullptr;

            auto rparen = consume(p, TK::RParen, "expected ')' after panic message");
            if (rparen.kind != TK::RParen) return nullptr;

            auto semi = consume(p, TK::SemiColon, "expected ';' after panic");
            if (semi.kind != TK::SemiColon) return nullptr;

            auto span = source::join(panic_tok.span, semi.span);
            return alloc<ast::PanicStmt>(p,
                                         ast::Stmt{ast::StmtKind::Panic, span}, msg);
        }

        [[nodiscard]] ast::BlockStmt *parse_block_stmt(Parser *p) {
            auto lbrace = consume(p, TK::LBrace, "expected '{' to start block");
            if (lbrace.kind != TK::LBrace) return nullptr;

            std::pmr::vector<ast::Stmt *> stmts{p->mr};
            while (!check(p, TK::RBrace) && !is_at_end(p)) {
                auto before = p->current;
                if (auto *s = parse_stmt(p)) stmts.push_back(s);
                else recover_stmt(p);
                ensure_progress(p, before);
            }

            auto rbrace = consume(p, TK::RBrace, "expected '}' to close block");
            if (rbrace.kind != TK::RBrace) return nullptr;

            auto span = source::join(lbrace.span, rbrace.span);
            return alloc<ast::BlockStmt>(p,
                                         ast::Stmt{ast::StmtKind::Block, span}, std::move(stmts));
        }

        [[nodiscard]] ast::Stmt *parse_expr_stmt(Parser *p) {
            auto *e = parse_expr(p);
            if (!e) return nullptr;
            auto semi = consume(p, TK::SemiColon, "expected ';' after expression");
            if (semi.kind != TK::SemiColon) return nullptr;
            return alloc<ast::ExprStmt>(p,
                                        ast::Stmt{ast::StmtKind::Expr, source::join(e->span, semi.span)}, e);
        }

        [[nodiscard]] ast::Expr *parse_expr(Parser *p) {
            return parse_assign_expr(p);
        }

        [[nodiscard]] ast::Expr *parse_assign_expr(Parser *p) {
            auto *lhs = parse_lor_expr(p);
            if (!lhs) return nullptr;

            auto k = peek(p).kind;
            if (k == TK::Eq || k == TK::PlusEq || k == TK::MinusEq ||
                k == TK::StarEq || k == TK::SlashEq || k == TK::ModEq) {
                auto op_tok = advance(p);
                auto op = to_assign_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_assign_expr(p); // right-assoc
                if (!rhs) return nullptr;
                return alloc<ast::AssignExpr>(p,
                                              ast::Expr{ast::ExprKind::Assign, source::join(lhs->span, rhs->span)},
                                              lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_lor_expr(Parser *p) {
            auto *lhs = parse_land_expr(p);
            if (!lhs) return nullptr;
            while (match(p, TK::Or)) {
                auto op_tok = previous(p);
                auto op = to_binary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_land_expr(p);
                if (!rhs) return nullptr;
                lhs = alloc<ast::BinaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Binary, source::join(lhs->span, rhs->span)},
                                             lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_land_expr(Parser *p) {
            auto *lhs = parse_eq_expr(p);
            if (!lhs) return nullptr;
            while (match(p, TK::And)) {
                auto op_tok = previous(p);
                auto op = to_binary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_eq_expr(p);
                if (!rhs) return nullptr;
                lhs = alloc<ast::BinaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Binary, source::join(lhs->span, rhs->span)},
                                             lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_eq_expr(Parser *p) {
            auto *lhs = parse_comp_expr(p);
            if (!lhs) return nullptr;
            while (check(p, TK::EqEq) || check(p, TK::Neq)) {
                auto op_tok = advance(p);
                auto op = to_binary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_comp_expr(p);
                if (!rhs) return nullptr;
                lhs = alloc<ast::BinaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Binary, source::join(lhs->span, rhs->span)},
                                             lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_comp_expr(Parser *p) {
            auto *lhs = parse_add_expr(p);
            if (!lhs) return nullptr;
            while (check(p, TK::Lt) || check(p, TK::Lte) ||
                   check(p, TK::Gt) || check(p, TK::Gte)) {
                auto op_tok = advance(p);
                auto op = to_binary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_add_expr(p);
                if (!rhs) return nullptr;
                lhs = alloc<ast::BinaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Binary, source::join(lhs->span, rhs->span)},
                                             lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_add_expr(Parser *p) {
            auto *lhs = parse_mul_expr(p);
            if (!lhs) return nullptr;
            while (check(p, TK::Plus) || check(p, TK::Minus)) {
                auto op_tok = advance(p);
                auto op = to_binary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_mul_expr(p);
                if (!rhs) return nullptr;
                lhs = alloc<ast::BinaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Binary, source::join(lhs->span, rhs->span)},
                                             lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_mul_expr(Parser *p) {
            auto *lhs = parse_unary_expr(p);
            if (!lhs) return nullptr;
            while (check(p, TK::Star) || check(p, TK::Slash) || check(p, TK::Mod)) {
                auto op_tok = advance(p);
                auto op = to_binary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *rhs = parse_unary_expr(p);
                if (!rhs) return nullptr;
                lhs = alloc<ast::BinaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Binary, source::join(lhs->span, rhs->span)},
                                             lhs, *op, op_tok.span, rhs);
            }
            return lhs;
        }

        [[nodiscard]] ast::Expr *parse_unary_expr(Parser *p) {
            if (check(p, TK::Not) || check(p, TK::Minus)) {
                auto op_tok = advance(p);
                auto op = to_unary_op(op_tok.kind);
                if (!op) return nullptr;
                auto *operand = parse_unary_expr(p);
                if (!operand) return nullptr;
                return alloc<ast::UnaryExpr>(p,
                                             ast::Expr{ast::ExprKind::Unary, source::join(op_tok.span, operand->span)},
                                             *op, op_tok.span, operand);
            }
            return parse_postfix_expr(p);
        }

        [[nodiscard]] ast::Expr *parse_postfix_expr(Parser *p) {
            auto *e = parse_primary_expr(p);
            if (!e) return nullptr;
            while (true) {
                if (match(p, TK::LParen)) {
                    auto before = p->diag->error_count;
                    auto args = parse_call_args(p);
                    if (p->diag->error_count > before) return nullptr;
                    auto rparen = previous(p);
                    e = alloc<ast::CallExpr>(p,
                                             ast::Expr{ast::ExprKind::Call, source::join(e->span, rparen.span)},
                                             e, std::move(args));
                } else if (match(p, TK::Dot)) {
                    auto name_tok = consume(p, TK::Ident, "expected member name after '.'");
                    if (name_tok.kind != TK::Ident) return nullptr;
                    e = alloc<ast::MemberExpr>(p,
                                               ast::Expr{ast::ExprKind::Member, source::join(e->span, name_tok.span)},
                                               e, to_name(name_tok));
                } else if (match(p, TK::LBrack)) {
                    auto *idx = parse_expr(p);
                    if (!idx) return nullptr;
                    auto rbrack = consume(p, TK::RBrack, "expected ']' after index");
                    if (rbrack.kind != TK::RBrack) return nullptr;
                    e = alloc<ast::IndexExpr>(p,
                                              ast::Expr{ast::ExprKind::Index, source::join(e->span, rbrack.span)},
                                              e, idx);
                } else {
                    break;
                }
            }
            return e;
        }

        [[nodiscard]] std::pmr::vector<ast::Expr *> parse_call_args(Parser *p) {
            std::pmr::vector<ast::Expr *> args{p->mr};
            if (!check(p, TK::RParen)) {
                while (true) {
                    auto before = p->current;
                    auto *a = parse_expr(p);
                    if (!a) return args; // caller detects via error_count
                    args.push_back(a);
                    if (!match(p, TK::Comma)) break;
                    ensure_progress(p, before);
                }
            }
            auto rparen = consume(p, TK::RParen, "expected ')' after arguments");
            // On failure, error emitted; caller detects via error_count
            (void) rparen;
            return args;
        }

        [[nodiscard]] ast::Expr *parse_primary_expr(Parser *p) {
            auto tok = peek(p);

            if (tok.kind == TK::Int) {
                advance(p);
                std::int64_t val{};
                auto [ptr, ec] = std::from_chars(
                    tok.lexeme.data(), tok.lexeme.data() + tok.lexeme.size(), val);
                if (ec != std::errc{}) {
                    diagnostics::error(p->diag, tok.span, "invalid integer literal");
                    return nullptr;
                }
                return alloc<ast::IntExpr>(p,
                                           ast::Expr{ast::ExprKind::Int, tok.span}, val);
            }

            if (tok.kind == TK::Float) {
                advance(p);
                std::string tmp{tok.lexeme};
                char *endp = nullptr;
                double val = std::strtod(tmp.c_str(), &endp);
                if (endp != tmp.c_str() + tmp.size()) {
                    diagnostics::error(p->diag, tok.span, "invalid float literal");
                    return nullptr;
                }
                return alloc<ast::FloatExpr>(p,
                                             ast::Expr{ast::ExprKind::Float, tok.span}, val);
            }

            if (tok.kind == TK::Str) {
                advance(p);
                return alloc<ast::StringExpr>(p,
                                              ast::Expr{ast::ExprKind::String, tok.span}, tok.lexeme);
            }

            if (tok.kind == TK::True || tok.kind == TK::False) {
                advance(p);
                return alloc<ast::BoolExpr>(p,
                                            ast::Expr{ast::ExprKind::Bool, tok.span}, tok.kind == TK::True);
            }

            if (tok.kind == TK::Nil) {
                advance(p);
                return alloc<ast::NilExpr>(p, ast::Expr{ast::ExprKind::Nil, tok.span});
            }

            if (tok.kind == TK::This) {
                advance(p);
                return alloc<ast::ThisExpr>(p, ast::Expr{ast::ExprKind::This, tok.span});
            }

            if (tok.kind == TK::Super) {
                advance(p);
                return alloc<ast::SuperExpr>(p, ast::Expr{ast::ExprKind::Super, tok.span});
            }

            if (tok.kind == TK::Ident) {
                advance(p);
                return alloc<ast::IdentExpr>(p,
                                             ast::Expr{ast::ExprKind::Ident, tok.span}, to_name(tok));
            }

            if (tok.kind == TK::New) {
                advance(p);
                auto *ty = parse_type(p);
                if (!ty) return nullptr;
                std::pmr::vector<ast::Expr *> args{p->mr};
                auto end = ty->span.end;
                if (match(p, TK::LParen)) {
                    auto before = p->diag->error_count;
                    args = parse_call_args(p);
                    if (p->diag->error_count > before) return nullptr;
                    end = previous(p).span.end;
                }
                return alloc<ast::NewExpr>(p,
                                           ast::Expr{ast::ExprKind::New, source::Span{tok.span.start, end}},
                                           ty, std::move(args));
            }

            if (tok.kind == TK::LParen) {
                advance(p);
                auto *first = parse_expr(p);
                if (!first) return nullptr;

                // (x) → GroupExpr ; (x, y, ...) → TupleExpr
                if (!check(p, TK::Comma)) {
                    auto rparen = consume(p, TK::RParen, "expected ')' after expression");
                    if (rparen.kind != TK::RParen) return nullptr;
                    return alloc<ast::GroupExpr>(p,
                                                 ast::Expr{ast::ExprKind::Group, source::join(tok.span, rparen.span)},
                                                 first);
                }
                std::pmr::vector<ast::Expr *> elems{p->mr};
                elems.push_back(first);
                while (match(p, TK::Comma)) {
                    auto *e = parse_expr(p);
                    if (!e) return nullptr;
                    elems.push_back(e);
                }
                auto rparen = consume(p, TK::RParen, "expected ')' after tuple elements");
                if (rparen.kind != TK::RParen) return nullptr;
                return alloc<ast::TupleExpr>(p,
                                             ast::Expr{ast::ExprKind::Tuple, source::join(tok.span, rparen.span)},
                                             std::move(elems));
            }

            if (tok.kind == TK::LBrack) {
                advance(p);
                std::pmr::vector<ast::Expr *> elems{p->mr};
                if (!check(p, TK::RBrack)) {
                    while (true) {
                        auto before = p->current;
                        auto *e = parse_expr(p);
                        if (!e) return nullptr;
                        elems.push_back(e);
                        if (!match(p, TK::Comma)) break;
                        ensure_progress(p, before);
                    }
                }
                auto rbrack = consume(p, TK::RBrack, "expected ']' after array literal");
                if (rbrack.kind != TK::RBrack) return nullptr;
                return alloc<ast::ArrayExpr>(p,
                                             ast::Expr{ast::ExprKind::Array, source::join(tok.span, rbrack.span)},
                                             std::move(elems));
            }

            error_here(p, "expected field or method in class body");
            return nullptr;
        }

        [[nodiscard]] ast::Type *parse_type(Parser *p) {
            return parse_func_type(p);
        }

        [[nodiscard]] ast::Type *parse_func_type(Parser *p) {
            auto *lhs = parse_postfix_type(p);
            if (!lhs) return nullptr;
            if (!match(p, TK::Arrow)) return lhs;

            auto *ret = parse_type(p);
            if (!ret) return nullptr;

            std::pmr::vector<ast::Type *> params{p->mr};
            if (lhs->kind == ast::TypeKind::Tuple) {
                auto *tuple = static_cast<ast::TupleType *>(lhs);
                params = std::move(tuple->elems);
            } else {
                params.push_back(lhs);
            }

            auto span = source::join(lhs->span, ret->span);
            return alloc<ast::FuncType>(p,
                                        ast::Type{ast::TypeKind::Func, span}, std::move(params), ret);
        }

        [[nodiscard]] ast::Type *parse_postfix_type(Parser *p) {
            auto *t = parse_applied_type(p);
            if (!t) return nullptr;
            while (match(p, TK::Question)) {
                auto q = previous(p);
                t = alloc<ast::OptionalType>(p,
                                             ast::Type{ast::TypeKind::Optional, source::join(t->span, q.span)}, t);
            }
            return t;
        }

        [[nodiscard]] ast::Type *parse_applied_type(Parser *p) {
            auto *base = parse_primary_type(p);
            if (!base) return nullptr;
            if (!check(p, TK::Lt)) return base;

            advance(p); // '<'
            std::pmr::vector<ast::Type *> args{p->mr};
            if (!check(p, TK::Gt)) {
                while (true) {
                    auto before = p->current;
                    auto *a = parse_type(p);
                    if (!a) return nullptr;
                    args.push_back(a);
                    if (!match(p, TK::Comma)) break;
                    ensure_progress(p, before);
                }
            }
            auto gt = consume(p, TK::Gt, "expected '>' to close type arguments");
            if (gt.kind != TK::Gt) return nullptr;

            auto span = source::join(base->span, gt.span);
            return alloc<ast::AppliedType>(p,
                                           ast::Type{ast::TypeKind::Applied, span}, base, std::move(args));
        }

        [[nodiscard]] ast::Type *parse_primary_type(Parser *p) {
            // Array: [T] (dynamic, size = -1) or [T; N] (fixed)
            if (check(p, TK::LBrack)) {
                auto lbrack = advance(p);
                auto *elem = parse_type(p);
                if (!elem) return nullptr;

                std::int64_t size = -1;
                if (match(p, TK::SemiColon)) {
                    auto size_tok = consume(p, TK::Int, "expected array size");
                    if (size_tok.kind != TK::Int) return nullptr;
                    std::int64_t s{};
                    auto [ptr, ec] = std::from_chars(
                        size_tok.lexeme.data(),
                        size_tok.lexeme.data() + size_tok.lexeme.size(), s);
                    if (ec != std::errc{}) {
                        diagnostics::error(p->diag, size_tok.span,
                                           "invalid array size");
                        return nullptr;
                    }
                    size = s;
                }
                auto rbrack = consume(p, TK::RBrack, "expected ']' to close array type");
                if (rbrack.kind != TK::RBrack) return nullptr;

                return alloc<ast::ArrayType>(p,
                                             ast::Type{ast::TypeKind::Array, source::join(lbrack.span, rbrack.span)},
                                             elem, size);
            }

            // Paren / tuple
            if (match(p, TK::LParen)) {
                auto lparen = previous(p);
                std::pmr::vector<ast::Type *> elems{p->mr};
                if (!check(p, TK::RParen)) {
                    while (true) {
                        auto before = p->current;
                        auto *t = parse_type(p);
                        if (!t) return nullptr;
                        elems.push_back(t);
                        if (!match(p, TK::Comma)) break;
                        ensure_progress(p, before);
                    }
                }
                auto rparen = consume(p, TK::RParen, "expected ')' to close paren type");
                if (rparen.kind != TK::RParen) return nullptr;
                return alloc<ast::TupleType>(p,
                                             ast::Type{ast::TypeKind::Tuple, source::join(lparen.span, rparen.span)},
                                             std::move(elems));
            }

            // Named
            auto name_tok = consume(p, TK::Ident, "expected type name");
            if (name_tok.kind != TK::Ident) return nullptr;
            return alloc<ast::NamedType>(p,
                                         ast::Type{ast::TypeKind::Named, name_tok.span}, to_name(name_tok));
        }
    }


    ast::Program parse(
        std::span<const syntax::Token> tokens,
        diagnostics::Engine *diag,
        std::pmr::memory_resource *mr) {
        Parser p{tokens, 0, mr, diag};
        std::pmr::vector<ast::Decl *> decls{mr};
        auto start = peek(&p).span.start;
        auto end = start;
        while (!is_at_end(&p)) {
            auto before = p.current;
            if (auto *d = parse_decl(&p)) {
                decls.push_back(d);
                end = previous(&p).span.end;
            } else {
                recover_decl(&p);
            }
            ensure_progress(&p, before);
        }
        return ast::Program{
            .span = source::Span{start, end},
            .decls = std::move(decls),
        };
    }
}
