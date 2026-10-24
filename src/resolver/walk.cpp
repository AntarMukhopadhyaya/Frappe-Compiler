//
// Created by antar on 19/10/26.
//

#include <frappe/resolver/resolver.hpp>

#include <cassert>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>
#include <frappe/resolver/intrinsics.hpp>


#include <frappe/fatal/fatality.hpp>


namespace frappe::resolver {
    namespace {
        using namespace frappe::ast;

        void declare_top_level(Resolver *r, const Decl *decl);

        void resolve_decl(Resolver *r, const Decl *decl);

        void resolve_fn_decl(Resolver *r, const FnDecl *fn, bool is_method);

        void resolve_class_decl(Resolver *r, const ClassDecl *cd);

        void resolve_interface_decl(Resolver *r, const InterfaceDecl *id);

        void resolve_enum_decl(Resolver *r, const EnumDecl *ed);

        void resolve_var_decl(Resolver *r, const VarDecl *vd);

        void resolve_block(Resolver *r, const BlockStmt *block);

        void resolve_stmt(Resolver *r, const Stmt *stmt);

        void resolve_expr(Resolver *r, const Expr *expr);

        void resolve_type(Resolver *r, const Type *t);

        [[nodiscard]] Symbol *symbol_for_type(Resolver *r, const Type *t);

        std::string_view import_name(const ImportDecl *imp) {
            if (imp->has_alias) return imp->alias.text;
            auto path = imp->path.text;
            std::size_t sep = std::string_view::npos;
            for (std::size_t i = 0; i < path.size(); ++i) {
                if (path[i] == '/' || path[i] == '\\') sep = i;
            }
            return sep == std::string_view::npos ? path : path.substr(sep + 1);
        }

        void declare_top_level(Resolver *r, const Decl *decl) {
            if (!decl) return;
            switch (decl->kind) {
                case DeclKind::Fn: {
                    auto *fn = static_cast<const FnDecl *>(decl);
                    Symbol *sym = declare(r, fn->name.text, SymbolKind::Fn,
                                          DeclRef{.fn = fn}, fn->name.span);
                    bind(r, fn, sym);
                    break;
                }
                case DeclKind::Class: {
                    auto *cd = static_cast<const ClassDecl *>(decl);
                    Symbol *sym = declare(r, cd->name.text, SymbolKind::Class,
                                          DeclRef{.class_ = cd}, cd->name.span);
                    bind(r, cd, sym);
                    break;
                }
                case DeclKind::Interface: {
                    auto *id = static_cast<const InterfaceDecl *>(decl);
                    Symbol *sym = declare(r, id->name.text, SymbolKind::Interface,
                                          DeclRef{.interface_ = id}, id->name.span);
                    bind(r, id, sym);
                    break;
                }
                case DeclKind::Enum: {
                    auto *ed = static_cast<const EnumDecl *>(decl);
                    Symbol *sym = declare(r, ed->name.text, SymbolKind::Enum,
                                          DeclRef{.enum_ = ed}, ed->name.span);
                    bind(r, ed, sym);
                    break;
                }
                case DeclKind::Var: {
                    auto *vd = static_cast<const VarDecl *>(decl);
                    Symbol *sym = declare(r, vd->name.text, SymbolKind::Var,
                                          DeclRef{.var_decl = vd}, vd->name.span);
                    bind(r, vd, sym);
                    break;
                }
                case DeclKind::Import: {
                    auto *im = static_cast<const ImportDecl *>(decl);
                    Symbol *sym = declare(r, import_name(im), SymbolKind::Module,
                                          DeclRef{.import_ = im}, im->path.span);
                    bind(r, im, sym);
                    break;
                }
            }
        }

        void resolve_decl(Resolver *r, const Decl *decl) {
            if (!decl) return;
            switch (decl->kind) {
                case DeclKind::Fn:
                    resolve_fn_decl(r, static_cast<const FnDecl *>(decl), false);
                    break;
                case DeclKind::Class:
                    resolve_class_decl(r, static_cast<const ClassDecl *>(decl));
                    break;
                case DeclKind::Interface:
                    resolve_interface_decl(r, static_cast<const InterfaceDecl *>(decl));
                    break;
                case DeclKind::Enum:
                    resolve_enum_decl(r, static_cast<const EnumDecl *>(decl));
                    break;
                case DeclKind::Var:
                    resolve_var_decl(r, static_cast<const VarDecl *>(decl));
                    break;
                case DeclKind::Import:
                    break;
            }
        }

        void resolve_fn_decl(Resolver *r, const FnDecl *fn, bool is_method) {
            const bool has_tp = !fn->type_params.empty();

            if (has_tp) {
                push_scope(r, ScopeKind::TypeParam);
                for (auto *tp: fn->type_params) {
                    static_cast<void>(declare(r, tp->name.text, SymbolKind::TypeParam,
                                              DeclRef{.type_param = tp}, tp->name.span));
                    if (tp->bound) resolve_type(r, tp->bound);
                }
            }

            if (fn->return_type) resolve_type(r, fn->return_type);

            push_scope(r, ScopeKind::Function);

            if (r->current_class != nullptr && is_method) {
                auto *this_sym = declare_synthetic(r, "this", SymbolKind::This, r->current_class);
                r->this_syms[fn] = this_sym;
                if (Symbol *base = class_base(r->current_class); base != nullptr) {
                    auto *super_sym = declare_synthetic(r, "super", SymbolKind::Super, base);
                    r->super_syms[fn] = super_sym;
                }
            }

            for (auto *param: fn->params) {
                Symbol *sym = declare(r, param->name.text, SymbolKind::Param,
                                      DeclRef{.param = param}, param->name.span);
                bind(r, param, sym);
                if (param->type_) resolve_type(r, param->type_);
            }

            if (fn->body) resolve_block(r, fn->body);

            pop_scope(r);
            if (has_tp) pop_scope(r);
        }

        void resolve_class_decl(Resolver *r, const ClassDecl *cd) {
            Symbol *class_sym = nullptr;
            if (auto it = r->current->symbols.find(cd->name.text);
                it != r->current->symbols.end()) {
                class_sym = it->second;
            }
            if (class_sym == nullptr) {
                FRAPPE_UNREACHABLE(cd->name.span,
                                   "class symbol missing — declare_top_level didn't run for '{}'",
                                   cd->name.text);
            }

            const bool has_tp = !cd->type_params.empty();
            if (has_tp) {
                push_scope(r, ScopeKind::TypeParam);
                for (auto *tp: cd->type_params) {
                    static_cast<void>(declare(r, tp->name.text, SymbolKind::TypeParam,
                                              DeclRef{.type_param = tp}, tp->name.span));
                    if (tp->bound) resolve_type(r, tp->bound);
                }
            }

            if (cd->base) {
                resolve_type(r, cd->base);
                if (Symbol *base_sym = symbol_for_type(r, cd->base); base_sym != nullptr) {
                    class_sym->base = base_sym;
                }
            }
            for (auto *iface: cd->interfaces) resolve_type(r, iface);

            push_scope(r, ScopeKind::Class);

            for (auto *field: cd->fields) {
                Symbol *sym = declare(r, field->name.text, SymbolKind::Field,
                                      DeclRef{.field = field}, field->name.span);
                bind(r, field, sym);
            }
            for (auto *method: cd->methods) {
                Symbol *sym = declare(r, method->name.text, SymbolKind::Method,
                                      DeclRef{.fn = method}, method->name.span);
                bind(r, method, sym);
            }

            for (auto *field: cd->fields) {
                if (field->type_) resolve_type(r, field->type_);
                if (field->init) resolve_expr(r, field->init);
            }

            Symbol *old = r->current_class;
            r->current_class = class_sym;
            for (auto *method: cd->methods) {
                resolve_fn_decl(r, method, true);
            }
            r->current_class = old;

            pop_scope(r);
            if (has_tp) pop_scope(r);
        }

        void resolve_enum_decl(Resolver *r, const EnumDecl *ed) {
            push_scope(r, ScopeKind::Enum);
            for (auto *variant: ed->variants) {
                static_cast<void>(declare(r, variant->name.text, SymbolKind::Field,
                                          DeclRef{.enum_variant = variant}, variant->name.span));
            }
            for (auto *variant: ed->variants) {
                if (variant->value) resolve_expr(r, variant->value);
            }
            pop_scope(r);
        }


        void resolve_interface_decl(Resolver *r, const InterfaceDecl *id) {
            const bool has_tp = !id->type_params.empty();
            if (has_tp) {
                push_scope(r, ScopeKind::TypeParam);
                for (auto *tp: id->type_params) {
                    static_cast<void>(declare(r, tp->name.text, SymbolKind::TypeParam,
                                              DeclRef{.type_param = tp}, tp->name.span));
                    if (tp->bound) resolve_type(r, tp->bound);
                }
            }

            for (auto *base: id->bases) resolve_type(r, base);

            push_scope(r, ScopeKind::Class);
            for (auto *method: id->methods) {
                static_cast<void>(declare(r, method->name.text, SymbolKind::Method,
                                          DeclRef{.fn = method}, method->name.span));
            }
            for (auto *method: id->methods) {
                const bool m_tp = !method->type_params.empty();
                if (m_tp) {
                    push_scope(r, ScopeKind::TypeParam);
                    for (auto *tp: method->type_params) {
                        static_cast<void>(declare(r, tp->name.text, SymbolKind::TypeParam,
                                                  DeclRef{.type_param = tp}, tp->name.span));
                        if (tp->bound) resolve_type(r, tp->bound);
                    }
                }
                if (method->return_type) resolve_type(r, method->return_type);
                for (auto *param: method->params) {
                    if (param->type_) resolve_type(r, param->type_);
                }
                if (m_tp) pop_scope(r);
            }
            pop_scope(r);

            if (has_tp) pop_scope(r);
        }


        void resolve_var_decl(Resolver *r, const VarDecl *vd) {
            if (vd->type_) resolve_type(r, vd->type_);
            if (vd->init) resolve_expr(r, vd->init);
        }

        void resolve_block(Resolver *r, const BlockStmt *block) {
            if (!block) return;
            push_scope(r, ScopeKind::Block);
            for (auto *stmt: block->stmts) resolve_stmt(r, stmt);
            pop_scope(r);
        }

        void resolve_stmt(Resolver *r, const Stmt *stmt) {
            if (!stmt) return;
            switch (stmt->kind) {
                case StmtKind::Expr: {
                    auto *es = static_cast<const ExprStmt *>(stmt);
                    resolve_expr(r, es->expr);
                    break;
                }
                case StmtKind::Var: {
                    auto *vs = static_cast<const VarStmt *>(stmt);
                    // type/init resolved in outer scope, then `x` declared
                    if (vs->type_) resolve_type(r, vs->type_);
                    if (vs->init) resolve_expr(r, vs->init);
                    Symbol *sym = declare(r, vs->name.text, SymbolKind::LocalVar,
                                          DeclRef{.var_stmt = vs}, vs->name.span);
                    bind(r, vs, sym);
                    break;
                }
                case StmtKind::Block:
                    resolve_block(r, static_cast<const BlockStmt *>(stmt));
                    break;
                case StmtKind::If: {
                    auto *is = static_cast<const IfStmt *>(stmt);
                    resolve_expr(r, is->condition);
                    resolve_block(r, is->then_branch);
                    if (is->else_branch) resolve_stmt(r, is->else_branch);
                    break;
                }
                case StmtKind::While: {
                    auto *ws = static_cast<const WhileStmt *>(stmt);
                    resolve_expr(r, ws->condition);
                    resolve_block(r, ws->body);
                    break;
                }
                case StmtKind::For: {
                    auto *fs = static_cast<const ForStmt *>(stmt);
                    push_scope(r, ScopeKind::Loop);
                    if (fs->init) resolve_stmt(r, fs->init);
                    if (fs->condition) resolve_expr(r, fs->condition);
                    if (fs->update) resolve_expr(r, fs->update);
                    resolve_block(r, fs->body);
                    pop_scope(r);
                    break;
                }

                case StmtKind::ForIn: {
                    auto *fi = static_cast<const ForInStmt *>(stmt);
                    resolve_expr(r, fi->iterable);
                    push_scope(r, ScopeKind::Loop);
                    static_cast<void>(declare(r, fi->variable.text, SymbolKind::ForInVar,
                                              DeclRef{.for_in = fi}, fi->variable.span));
                    resolve_block(r, fi->body);
                    pop_scope(r);
                    break;
                }
                case StmtKind::Switch: {
                    auto *sw = static_cast<const SwitchStmt *>(stmt);
                    resolve_expr(r, sw->subject);
                    for (auto *cc: sw->cases) {
                        for (auto *val: cc->values) resolve_expr(r, val);
                        push_scope(r, ScopeKind::Block);
                        for (auto *inner: cc->body) resolve_stmt(r, inner);
                        pop_scope(r);
                    }
                    if (!sw->default_body.empty()) {
                        push_scope(r, ScopeKind::Block);
                        for (auto *inner: sw->default_body) resolve_stmt(r, inner);
                        pop_scope(r);
                    }
                    break;
                }
                case StmtKind::Break:
                case StmtKind::Continue:
                    break;
                case StmtKind::Return: {
                    auto *rs = static_cast<const ReturnStmt *>(stmt);
                    if (rs->value) resolve_expr(r, rs->value);
                    break;
                }
                case StmtKind::Defer: {
                    auto *ds = static_cast<const DeferStmt *>(stmt);
                    if (ds->body) resolve_stmt(r, ds->body);
                    break;
                }
                case StmtKind::Panic: {
                    auto *ps = static_cast<const PanicStmt *>(stmt);
                    if (ps->message) resolve_expr(r, ps->message);
                    break;
                }
            }
        }

        void resolve_expr(Resolver *r, const Expr *expr) {
            if (!expr) return;
            switch (expr->kind) {
                case ExprKind::Int:
                case ExprKind::Float:
                case ExprKind::String:
                case ExprKind::Bool:
                case ExprKind::Nil:
                    break;

                case ExprKind::Ident: {
                    auto *ie = static_cast<const IdentExpr *>(expr);
                    Symbol *sym = resolve(r, ie->name.text);
                    if (sym) {
                        bind(r, ie, sym);
                    } else if (!resolver::find_intrinsic(ie->name.text)) {
                        // Not a symbol and not an intrinsic — genuinely unknown.
                        diagnostics::report_error(r->diag, ie->name.span,
                                                  "unknown identifier '{}'", ie->name.text);
                    }
                    // If it's an intrinsic: no symbol to bind, no error to report. The
                    // checker dispatches on the name via its own find_intrinsic call.
                    break;
                }
                case ExprKind::This: {
                    auto *te = static_cast<const ThisExpr *>(expr);
                    Symbol *sym = resolve(r, "this");
                    if (sym) {
                        bind(r, te, sym);
                    } else {
                        diagnostics::error(r->diag, te->span,
                                           "'this' is only valid inside a method");
                    }
                    break;
                }
                case ExprKind::Super: {
                    auto *se = static_cast<const SuperExpr *>(expr);
                    Symbol *sym = resolve(r, "super");
                    if (sym) {
                        bind(r, se, sym);
                    } else {
                        diagnostics::error(r->diag, se->span,
                                           "'super' requires a class with a base class");
                    }
                    break;
                }
                case ExprKind::Binary: {
                    auto *be = static_cast<const BinaryExpr *>(expr);
                    resolve_expr(r, be->left);
                    resolve_expr(r, be->right);
                    break;
                }
                case ExprKind::Unary: {
                    auto *ue = static_cast<const UnaryExpr *>(expr);
                    resolve_expr(r, ue->operand);
                    break;
                }
                case ExprKind::Assign: {
                    auto *ae = static_cast<const AssignExpr *>(expr);
                    resolve_expr(r, ae->left);
                    resolve_expr(r, ae->right);
                    break;
                }
                case ExprKind::Call: {
                    auto *ce = static_cast<const CallExpr *>(expr);
                    resolve_expr(r, ce->callee);
                    for (auto *arg: ce->args) resolve_expr(r, arg);
                    break;
                }
                case ExprKind::Member: {
                    auto *me = static_cast<const MemberExpr *>(expr);
                    // only object is resolved; member name is type-resolved by checker
                    resolve_expr(r, me->object);
                    break;
                }
                case ExprKind::Index: {
                    auto *ix = static_cast<const IndexExpr *>(expr);
                    resolve_expr(r, ix->object);
                    resolve_expr(r, ix->index);
                    break;
                }
                case ExprKind::New: {
                    auto *ne = static_cast<const NewExpr *>(expr);
                    resolve_type(r, ne->type_);
                    for (auto *arg: ne->args) resolve_expr(r, arg);
                    break;
                }
                case ExprKind::Group: {
                    auto *ge = static_cast<const GroupExpr *>(expr);
                    resolve_expr(r, ge->expr);
                    break;
                }
                case ExprKind::Array: {
                    auto *ae = static_cast<const ArrayExpr *>(expr);
                    for (auto *elem: ae->elements) resolve_expr(r, elem);
                    break;
                }
                case ExprKind::Tuple: {
                    auto *te = static_cast<const TupleExpr *>(expr);
                    for (auto *elem: te->elements) resolve_expr(r, elem);
                    break;
                }
            }
        }

        void resolve_type(Resolver *r, const Type *t) {
            if (!t) return;
            switch (t->kind) {
                case TypeKind::Named: {
                    auto *nt = static_cast<const NamedType *>(t);
                    Symbol *sym = resolve(r, nt->name.text);
                    if (sym) {
                        bind(r, nt, sym);
                    } else {
                        diagnostics::report_error(r->diag, nt->name.span,
                                                  "unknown type '{}'", nt->name.text);
                    }
                    break;
                }
                case TypeKind::Array: {
                    auto *at = static_cast<const ArrayType *>(t);
                    resolve_type(r, at->element);
                    break;
                }
                case TypeKind::Optional: {
                    auto *ot = static_cast<const OptionalType *>(t);
                    resolve_type(r, ot->inner);
                    break;
                }
                case TypeKind::Applied: {
                    auto *ap = static_cast<const AppliedType *>(t);
                    resolve_type(r, ap->base);
                    for (auto *arg: ap->args) resolve_type(r, arg);
                    break;
                }
                case TypeKind::Func: {
                    auto *ft = static_cast<const FuncType *>(t);
                    for (auto *p: ft->params) resolve_type(r, p);
                    if (ft->ret) resolve_type(r, ft->ret);
                    break;
                }
                case TypeKind::Tuple: {
                    auto *tt = static_cast<const TupleType *>(t);
                    for (auto *elem: tt->elems) resolve_type(r, elem);
                    break;
                }
            }
        }

        Symbol *symbol_for_type(Resolver *r, const Type *t) {
            if (!t) return nullptr;
            switch (t->kind) {
                case TypeKind::Named: {
                    auto *nt = static_cast<const NamedType *>(t);
                    return lookup(r, nt);
                }
                case TypeKind::Applied: {
                    auto *ap = static_cast<const AppliedType *>(t);
                    return symbol_for_type(r, ap->base);
                }
                case TypeKind::Array:
                case TypeKind::Optional:
                case TypeKind::Func:
                case TypeKind::Tuple:
                    return nullptr;
            }
            return nullptr;
        }

        // Tri-color DFS over the class inheritance graph.
        //
        // Contract:
        //   - Must run after every class's base link has been resolved
        //     (i.e., after the second pass of `resolve_program`).
        //   - Operates on the symbol graph (`Symbol::base`), not the AST —
        //     no re-resolution, no scope walking.
        //   - Reports each cycle exactly once, at the first class re-entered
        //     while Gray (on the current DFS stack).
        //   - Diamond-style sharing (two classes inheriting the same base) is
        //     legal — that's a Black revisit, not a cycle
        void validate_inheritance(Resolver *r) {
            enum class Color : std::uint8_t { White, Gray, Black };
            std::pmr::unordered_map<const Symbol *, Color> colors{r->mr};
            std::function<bool(const Symbol *)> visit = [&](const Symbol *sym) -> bool {
                auto &color = colors[sym];
                if (color == Color::Black) return true;

                if (color == Color::Gray) {
                    // Report on the class that closed the cycle.
                    source::Span span{};
                    if (sym->kind == SymbolKind::Class) {
                        span = sym->decl.class_->name.span;
                    }
                    diagnostics::report_error(r->diag, span,
                                              "cyclic inheritance: '{}' inherits from itself", sym->name);
                    return false;
                }

                color = Color::Gray;
                if (sym->base != nullptr && sym->base->kind == SymbolKind::Class) {
                    if (!visit(sym->base)) return false;
                }
                color = Color::Black;
                return true;
            };

            for (const auto &[name, sym]: r->module_scope->symbols) {
                (void) name;
                if (sym->kind != SymbolKind::Class) continue;
                if (!visit(sym)) return;
            }
        }
    }

    void resolve_program(Resolver *r, const Program &program) {
        for (auto *decl: program.decls) declare_top_level(r, decl);
        for (auto *decl: program.decls) resolve_decl(r, decl);
        validate_inheritance(r);
    }
}
