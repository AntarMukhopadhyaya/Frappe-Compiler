//
// Created by antar on 19/10/26.
//

#include <frappe/checker/checker.hpp>

#include <cassert>

#include "checker_internal.hpp"

namespace frappe::checker {
    namespace {
        struct Visiting_Guard {
            std::pmr::unordered_set<const resolver::Symbol *> &set;
            const resolver::Symbol *sym;

            ~Visiting_Guard() {
                set.erase(sym);
            }
        };
    }

    Checker make_checker(resolver::Resolver *res, diagnostics::Engine *diag, std::pmr::memory_resource *mr) {
        return Checker{
            .mr = mr,
            .diag = diag,
            .resolver_ = res,
            .store = types::make_store(mr),
            .node_types = std::pmr::unordered_map<const void *, types::Type *>{mr},
            .symbol_types = std::pmr::unordered_map<const resolver::Symbol *, types::Type *>{mr},
            .visiting = std::pmr::unordered_set<const resolver::Symbol *>{mr},
            .current_fn = nullptr,
            .current_class = nullptr,
            .subst = nullptr,
            .instances = std::pmr::unordered_map<std::pmr::string, Checked_Fn *>{mr},
            .worklist = std::pmr::vector<Pending_Instance>{mr},
            .call_names = std::pmr::unordered_map<const void *, std::pmr::string>{mr},
            .call_names_out = nullptr,
        };
    }

    void check_program(Checker *c, const ast::Program &program) {
        for (auto *decl: program.decls) {
            detail::check_decl(c, decl);
        }
        detail::drain_worklist(c);
    }

    namespace detail {
        types::Type *resolve_ast_type(Checker *c, const ast::Type *t) {
            if (!t) return c->store.void_type;
            // Cache pert instance context wins else global
            if (c->node_types_out) {
                if (auto it = c->node_types_out->find(t); it != c->node_types_out->end()) {
                    return it->second;
                }
            } else {
                if (auto it = c->node_types.find(t); it != c->node_types.end()) {
                    return it->second;
                }
            }
            types::Type *result = c->store.error_type;

            switch (t->kind) {
                case ast::TypeKind::Named: {
                    auto *nt = static_cast<const ast::NamedType *>(t);
                    auto *sym = resolver::lookup(c->resolver_, nt);
                    result = symbol_to_type(c, sym, nt->name.span);
                    break;
                }
                case ast::TypeKind::Array: {
                    auto *at = static_cast<const ast::ArrayType *>(t);
                    auto *elem = resolve_ast_type(c, at->element);
                    result = types::array_type(&c->store, elem, at->size);
                    break;
                }
                case ast::TypeKind::Optional: {
                    auto *ot = static_cast<const ast::OptionalType *>(t);
                    auto *inner = resolve_ast_type(c, ot->inner);
                    result = types::optional_type(&c->store, inner);
                    break;
                }
                case ast::TypeKind::Applied: {
                    auto *ap = static_cast<const ast::AppliedType *>(t);
                    diagnostics::report_error(c->diag, ap->span,
                                              "generic types not supported in v1");
                    result = c->store.error_type;
                    break;
                }
                case ast::TypeKind::Func: {
                    auto *ft = static_cast<const ast::FuncType *>(t);
                    std::pmr::vector<types::Type *> params{c->store.mr};
                    params.reserve(ft->params.size());
                    for (auto *p: ft->params) params.push_back(resolve_ast_type(c, p));
                    auto *ret = resolve_ast_type(c, ft->ret);
                    result = types::func_type(&c->store, params, ret);
                    break;
                }
                case ast::TypeKind::Tuple: {
                    auto *tt = static_cast<const ast::TupleType *>(t);
                    std::pmr::vector<types::Type *> elems{c->store.mr};
                    elems.reserve(tt->elems.size());
                    for (auto *e: tt->elems) elems.push_back(resolve_ast_type(c, e));
                    result = types::tuple_type(&c->store, elems);
                    break;
                }
            }

            set_type(c, t, result);
            return result;
        }

        types::Type *symbol_to_type(Checker *c, const resolver::Symbol *sym,
                                    const source::Span &span) {
            if (!sym) return c->store.error_type;

            auto k = sym->kind;
            if (k == resolver::SymbolKind::BuiltinType) {
                return types::builtin_type(&c->store, sym->builtin);
            }
            if (k == resolver::SymbolKind::Class) {
                return types::named_type(&c->store, types::NamedTypeDeclRef{
                                             .kind = types::NamedDeclKind::Class,
                                             .class_ = sym->decl.class_,
                                         });
            }
            if (k == resolver::SymbolKind::Interface) {
                return types::named_type(&c->store, types::NamedTypeDeclRef{
                                             .kind = types::NamedDeclKind::Interface,
                                             .interface_ = sym->decl.interface_,
                                         });
            }
            if (k == resolver::SymbolKind::Enum) {
                return types::named_type(&c->store, types::NamedTypeDeclRef{
                                             .kind = types::NamedDeclKind::Enum,
                                             .enum_ = sym->decl.enum_,
                                         });
            }
            if (k == resolver::SymbolKind::TypeParam) {
                if (c->subst) {
                    auto it = c->subst->bindings.find(sym->name);
                    if (it != c->subst->bindings.end()) return it->second;
                }
                diagnostics::report_error(c->diag, span,
                                          "unbound type parameter '{}'", sym->name);
                return c->store.error_type;
            }
            diagnostics::report_error(c->diag, span, "'{}' is not a type", sym->name);
            return c->store.error_type;
        }

        bool class_implements(const Checker *c,
                              const ast::ClassDecl *cls,
                              const ast::InterfaceDecl *iface) {
            if (!cls || !iface) return false;
            for (const ast::ClassDecl *walk = cls; walk; walk = get_base_class(c, walk)) {
                for (auto *iface_ty: walk->interfaces) {
                    if (iface_ty->kind != ast::TypeKind::Named) continue;
                    auto *nt = static_cast<const ast::NamedType *>(iface_ty);
                    auto *sym = resolver::lookup(c->resolver_, nt);
                    if (!sym || sym->kind != resolver::SymbolKind::Interface) continue;
                    if (sym->decl.interface_ == iface) return true;
                }
            }
            return false;
        }


        bool check_assignable(const Checker *c, const types::Type *expected, const types::Type *actual,
                              const source::Span &span) {
            if (!expected || !actual) return true;
            if (types::is_error(expected) || types::is_error(actual)) return true;
            if (types::type_eq(expected, actual)) return true;

            if (actual->kind == types::TypeKind::Builtin) {
                auto *ab = static_cast<const types::BuiltinType *>(actual);
                if (ab->b == resolver::Builtin::Nil
                    && expected->kind == types::TypeKind::Optional) {
                    return true;
                }
            }

            // Derived classes are assignable to their base classes, including
            // transitive inheritance. Classes share the LLVM `ptr` representation,
            // and base fields come first in the struct layout, so no codegen changes
            // are needed. Interface conformance and generic-class variance are not
            // supported yet and will be handled separately.
            if (expected->kind == types::TypeKind::Named
                && actual->kind == types::TypeKind::Named) {
                auto *exp_n = static_cast<const types::NamedType *>(expected);
                auto *act_n = static_cast<const types::NamedType *>(actual);
                if (exp_n->decl.kind == types::NamedDeclKind::Class
                    && act_n->decl.kind == types::NamedDeclKind::Class) {
                    if (is_subclass(c, act_n->decl.class_, exp_n->decl.class_)) {
                        return true;
                    }
                }
                if (exp_n->decl.kind == types::NamedDeclKind::Interface
                    && act_n->decl.kind == types::NamedDeclKind::Class) {
                    if (class_implements(c, act_n->decl.class_, exp_n->decl.interface_)) {
                        return true;
                    }
                }
            }

            diagnostics::report_error(c->diag, span, "type mismatch: expected {}, got {}",
                                      types::type_name(c->store.mr, expected),
                                      types::type_name(c->store.mr, actual));
            return false;
        }

        namespace {
            bool is_value_kind(resolver::SymbolKind k) {
                return k == resolver::SymbolKind::Var
                       || k == resolver::SymbolKind::LocalVar
                       || k == resolver::SymbolKind::ForInVar
                       || k == resolver::SymbolKind::Param
                       || k == resolver::SymbolKind::Field
                       || k == resolver::SymbolKind::Fn
                       || k == resolver::SymbolKind::Method;
            }
        }

        types::Type *type_of_symbol(Checker *c, const resolver::Symbol *sym) {
            if (!sym) return c->store.error_type;
            if (auto it = c->symbol_types.find(sym); it != c->symbol_types.end()) {
                return it->second;
            }
            if (c->visiting.contains(sym)) {
                diagnostics::report_error(c->diag, {}, "circular type inference involving '{}'", sym->name);
                return c->store.error_type;
            }

            c->visiting.insert(sym);
            Visiting_Guard guard{c->visiting, sym};

            types::Type *result = c->store.error_type;

            if (!is_value_kind(sym->kind)) {
                c->symbol_types[sym] = result;
                return result;
            }

            auto k = sym->kind;
            if (k == resolver::SymbolKind::Param) {
                result = resolve_ast_type(c, sym->decl.param->type_);
            } else if (k == resolver::SymbolKind::Var) {
                auto *vd = sym->decl.var_decl;
                if (vd->type_) result = resolve_ast_type(c, vd->type_);
                else if (vd->init) result = check_expr(c, vd->init);
                else result = c->store.unknown_type;
            } else if (k == resolver::SymbolKind::LocalVar) {
                auto *vs = sym->decl.var_stmt;
                if (vs->type_) result = resolve_ast_type(c, vs->type_);
                else if (vs->init) result = check_expr(c, vs->init);
                else result = c->store.unknown_type;
            } else if (k == resolver::SymbolKind::ForInVar) {
                // Should already be cached by check_stmt's ForIn handling.
                result = c->store.unknown_type;
            } else if (k == resolver::SymbolKind::Field) {
                auto *fd = sym->decl.field;
                if (fd->type_) result = resolve_ast_type(c, fd->type_);
                else if (fd->init) result = check_expr(c, fd->init);
                else result = c->store.unknown_type;
            } else if (k == resolver::SymbolKind::Fn || k == resolver::SymbolKind::Method) {
                auto *fn = sym->decl.fn;
                if (!fn->type_params.empty()) {
                    diagnostics::report_error(c->diag, fn->name.span,
                                              "generic function '{}' cannot be used as value", sym->name);
                    result = c->store.error_type;
                } else {
                    std::pmr::vector<types::Type *> params{c->store.mr};
                    params.reserve(fn->params.size());
                    for (auto *p: fn->params) params.push_back(resolve_ast_type(c, p->type_));
                    auto *ret = resolve_ast_type(c, fn->return_type);
                    result = types::func_type(&c->store, params, ret);
                }
            }

            c->symbol_types[sym] = result;
            return result;
        }
    }

    types::Type *resolve_type(Checker *c, const ast::Type *t) {
        return detail::resolve_ast_type(c, t);
    }
}
