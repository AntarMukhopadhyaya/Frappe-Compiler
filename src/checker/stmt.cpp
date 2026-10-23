//
// Created by antar on 19/10/26.
//
#include <frappe/checker/checker.hpp>

#include <span>
#include <string_view>

#include <frappe/ast/stmt.hpp>

#include "checker_internal.hpp"

namespace frappe::checker::detail {
    namespace {
        using ast::StmtKind;
        using types::Type;
        using types::TypeKind;

        void require_bool(const Checker *c, const Type *t, const source::Span &span,
                          std::string_view what) {
            if (types::is_error(t)) return;
            if (!types::type_eq(t, c->store.bool_type)) {
                diagnostics::report_error(c->diag, span, "{} must be Bool, got {}", what,
                                          types::type_name(c->store.mr, t));
            }
        }

        void check_var_stmt(Checker *c, const ast::VarStmt *vs) {
            if (vs->type_) {
                auto *declared = resolve_ast_type(c, vs->type_);
                if (vs->init) {
                    auto *it = check_expr_against(c, vs->init, declared);
                    check_assignable(c, declared, it, vs->init->span);
                }
                if (auto *sym = resolver::lookup(c->resolver_, vs); sym) {
                    c->symbol_types[sym] = declared;
                }
            } else if (vs->init) {
                auto *it = check_expr(c, vs->init);
                if (types::is_error(it)) return;
                if (it->kind == TypeKind::Builtin) {
                    auto *bt = static_cast<const types::BuiltinType *>(it);
                    if (bt->b == resolver::Builtin::Nil) {
                        diagnostics::report_error(c->diag, vs->init->span,
                                                  "cannot infer type from 'nil'");
                    }
                }
                if (auto *sym = resolver::lookup(c->resolver_, vs); sym) {
                    c->symbol_types[sym] = it;
                }
            }
        }

        void check_return(Checker *c, const ast::ReturnStmt *rs) {
            Type *expected = c->store.void_type;
            if (c->current_fn && c->current_fn->return_type) {
                expected = resolve_ast_type(c, c->current_fn->return_type);
            }
            if (!rs->value) {
                if (!types::type_eq(expected, c->store.void_type)) {
                    diagnostics::report_error(c->diag, rs->span, "missing return value");
                }
                return;
            }
            if (types::type_eq(expected, c->store.void_type)) {
                diagnostics::report_error(c->diag, rs->span,
                                          "cannot return a value from a void function");
                static_cast<void>(check_expr(c, rs->value));
                return;
            }
            auto *it = check_expr(c, rs->value);
            check_assignable(c, expected, it, rs->value->span);
        }

        [[nodiscard]] bool contains_return(const ast::Stmt *s) {
            if (!s) return false;
            switch (s->kind) {
                case ast::StmtKind::Return:
                    return true;

                case ast::StmtKind::Block: {
                    auto *bs = static_cast<const ast::BlockStmt *>(s);
                    for (auto *inner: bs->stmts) {
                        if (contains_return(inner)) return true;
                    }
                    return false;
                }
                case ast::StmtKind::If: {
                    auto *is = static_cast<const ast::IfStmt *>(s);
                    if (contains_return(is->then_branch)) return true;
                    if (is->else_branch && contains_return(is->else_branch)) return true;
                    return false;
                }
                case ast::StmtKind::While: {
                    auto *ws = static_cast<const ast::WhileStmt *>(s);
                    return contains_return(ws->body);
                }
                case ast::StmtKind::For: {
                    auto *fs = static_cast<const ast::ForStmt *>(s);
                    if (fs->init && contains_return(fs->init)) return true;
                    return contains_return(fs->body);
                }
                case ast::StmtKind::ForIn: {
                    auto *fi = static_cast<const ast::ForInStmt *>(s);
                    return contains_return(fi->body);
                }
                case ast::StmtKind::Switch: {
                    auto *sw = static_cast<const ast::SwitchStmt *>(s);
                    for (auto *cc: sw->cases) {
                        for (auto *inner: cc->body) {
                            if (contains_return(inner)) return true;
                        }
                    }
                    for (auto *inner: sw->default_body) {
                        if (contains_return(inner)) return true;
                    }
                    return false;
                }
                case ast::StmtKind::Defer: {
                    // Don't recurse into nested defer bodies — the inner defer
                    // reports its own returns; the outer would duplicate them.
                    return false;
                }

                case ast::StmtKind::Expr:
                case ast::StmtKind::Var:
                case ast::StmtKind::Break:
                case ast::StmtKind::Continue:
                case ast::StmtKind::Panic:
                    return false;
            }
            std::unreachable();
        }
    }

    // ============================================================================
    // Public — within detail namespace
    // ============================================================================

    void check_stmt(Checker *c, const ast::Stmt *s) {
        if (!s) return;
        switch (s->kind) {
            case StmtKind::Expr: {
                auto *es = static_cast<const ast::ExprStmt *>(s);
                static_cast<void>(check_expr(c, es->expr));
                break;
            }
            case StmtKind::Var: {
                check_var_stmt(c, static_cast<const ast::VarStmt *>(s));
                break;
            }
            case StmtKind::Block: {
                auto *bs = static_cast<const ast::BlockStmt *>(s);
                for (auto *inner: bs->stmts) check_stmt(c, inner);
                break;
            }
            case StmtKind::If: {
                auto *is = static_cast<const ast::IfStmt *>(s);
                auto *cond = check_expr(c, is->condition);
                require_bool(c, cond, is->condition->span, "if condition");
                check_stmt(c, is->then_branch);
                if (is->else_branch) check_stmt(c, is->else_branch);
                break;
            }
            case StmtKind::While: {
                auto *ws = static_cast<const ast::WhileStmt *>(s);
                auto *cond = check_expr(c, ws->condition);
                require_bool(c, cond, ws->condition->span, "while condition");
                check_stmt(c, ws->body);
                break;
            }
            case StmtKind::For: {
                auto *fs = static_cast<const ast::ForStmt *>(s);
                if (fs->init) check_stmt(c, fs->init);
                if (fs->condition) {
                    auto *cond = check_expr(c, fs->condition);
                    require_bool(c, cond, fs->condition->span, "for condition");
                }
                if (fs->update) static_cast<void>(check_expr(c, fs->update));
                check_stmt(c, fs->body);
                break;
            }
            case StmtKind::ForIn: {
                auto *fi = static_cast<const ast::ForInStmt *>(s);
                auto *iter = check_expr(c, fi->iterable);
                Type *elem_type = c->store.error_type;
                if (iter->kind == TypeKind::Array) {
                    elem_type = static_cast<const types::ArrayType *>(iter)->element;
                } else if (!types::is_error(iter)) {
                    diagnostics::report_error(c->diag, fi->iterable->span,
                                              "for-in requires an array");
                }
                if (auto *sym = resolver::lookup(c->resolver_, fi); sym) {
                    c->symbol_types[sym] = elem_type;
                }
                check_stmt(c, fi->body);
                break;
            }
            case StmtKind::Switch: {
                auto *sw = static_cast<const ast::SwitchStmt *>(s);
                static_cast<void>(check_expr(c, sw->subject));
                // TODO(v2): use subject type for exhaustiveness checking
                for (auto *cc: sw->cases) {
                    for (auto *v: cc->values) static_cast<void>(check_expr(c, v));
                    for (auto *inner: cc->body) check_stmt(c, inner);
                }
                for (auto *inner: sw->default_body) check_stmt(c, inner);
                break;
            }
            case StmtKind::Return:
                check_return(c, static_cast<const ast::ReturnStmt *>(s));
                break;
            case StmtKind::Defer: {
                auto *ds = static_cast<const ast::DeferStmt *>(s);
                if (ds->body) check_stmt(c, ds->body);

                // `return` inside a defer body would produce a second return after
                // the deferred call runs — breaking cleanup order. Reject it.
                if (contains_return(ds->body)) {
                    diagnostics::report_error(c->diag, s->span,
                                              "'return' not allowed inside 'defer' (would run after deferred calls)");
                }
                break;
            }
            case StmtKind::Panic: {
                auto *ps = static_cast<const ast::PanicStmt *>(s);
                auto *msg_ty = check_expr(c, ps->message);
                if (!types::is_error(msg_ty)
                    && !types::type_eq(msg_ty, c->store.string_type)) {
                    diagnostics::report_error(c->diag, ps->message->span,
                                              "panic message must be String, got {}",
                                              types::type_name(c->store.mr, msg_ty));
                }
                break;
            }
            case StmtKind::Break:
            case StmtKind::Continue:
                break;
        }
    }
} // namespace frappe::checker::detail
