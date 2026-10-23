//
// Created by antar on 19/10/26.
//
#include <frappe/checker/checker.hpp>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <string_view>
#include <utility>
#include <frappe/ast/decl.hpp>
#include "frappe/syntax/constants.hpp"
#include "checker_internal.hpp"


namespace frappe::checker::detail {
    namespace {
        bool ast_types_match(const ast::Type *a, const ast::Type *b) {
            if (!a || !b) return !a && !b;
            if (a->kind != b->kind) return false;
            switch (a->kind) {
                case ast::TypeKind::Named:
                    return static_cast<const ast::NamedType *>(a)->name.text
                           == static_cast<const ast::NamedType *>(b)->name.text;
                case ast::TypeKind::Array: {
                    auto *aa = static_cast<const ast::ArrayType *>(a);
                    auto *ab = static_cast<const ast::ArrayType *>(b);
                    return aa->size == ab->size
                           && ast_types_match(aa->element, ab->element);
                }
                case ast::TypeKind::Optional:
                    return ast_types_match(
                        static_cast<const ast::OptionalType *>(a)->inner,
                        static_cast<const ast::OptionalType *>(b)->inner);
                case ast::TypeKind::Func: {
                    auto *fa = static_cast<const ast::FuncType *>(a);
                    auto *fb = static_cast<const ast::FuncType *>(b);
                    if (fa->params.size() != fb->params.size()) return false;
                    for (std::size_t i = 0; i < fa->params.size(); ++i) {
                        if (!ast_types_match(fa->params[i], fb->params[i])) return false;
                    }
                    return ast_types_match(fa->ret, fb->ret);
                }
                case ast::TypeKind::Tuple: {
                    auto *ta = static_cast<const ast::TupleType *>(a);
                    auto *tb = static_cast<const ast::TupleType *>(b);
                    if (ta->elems.size() != tb->elems.size()) return false;
                    for (std::size_t i = 0; i < ta->elems.size(); ++i) {
                        if (!ast_types_match(ta->elems[i], tb->elems[i])) return false;
                    }
                    return true;
                }
                case ast::TypeKind::Applied: {
                    auto *aa = static_cast<const ast::AppliedType *>(a);
                    auto *ab = static_cast<const ast::AppliedType *>(b);
                    // `base` is itself a Type — typically a NamedType holding
                    // the class/interface name. Recurse into it so e.g.
                    // `Box<Int>` vs `Vec<Int>` compares by structural name
                    // equality of the base type, not by whatever some
                    // hypothetical `.name` field would have held.
                    if (!ast_types_match(aa->base, ab->base)) return false;
                    if (aa->args.size() != ab->args.size()) return false;
                    for (std::size_t i = 0; i < aa->args.size(); ++i) {
                        if (!ast_types_match(aa->args[i], ab->args[i])) return false;
                    }
                    return true;
                }
            }
            return false;
        }

        bool signatures_match(const ast::FnDecl *base, const ast::FnDecl *derived) {
            if (base->params.size() != derived->params.size()) return false;
            if (base->type_params.size() != derived->type_params.size()) return false;
            for (std::size_t i = 0; i < base->params.size(); ++i) {
                if (!ast_types_match(base->params[i]->type_, derived->params[i]->type_)) {
                    return false;
                }
            }
            return ast_types_match(base->return_type, derived->return_type);
        }

        // Formatting helpers for error messages. Produces "(Int, String) -> Bool"
        // and "()" etc. — matching the display format write_type_name uses for
        // resolved types.
        void append_ast_type_name(std::pmr::string &out, const ast::Type *t) {
            if (!t) {
                out += "Void";
                return;
            }
            switch (t->kind) {
                case ast::TypeKind::Named:
                    out += static_cast<const ast::NamedType *>(t)->name.text;
                    break;
                case ast::TypeKind::Array: {
                    auto *a = static_cast<const ast::ArrayType *>(t);
                    out += '[';
                    append_ast_type_name(out, a->element);
                    if (a->size < 0) out += ']';
                    else {
                        out += "; ";
                        out += std::to_string(a->size);
                        out += ']';
                    }
                    break;
                }
                case ast::TypeKind::Optional:
                    append_ast_type_name(out, static_cast<const ast::OptionalType *>(t)->inner);
                    out += '?';
                    break;
                case ast::TypeKind::Func: {
                    auto *f = static_cast<const ast::FuncType *>(t);
                    out += '(';
                    for (std::size_t i = 0; i < f->params.size(); ++i) {
                        if (i > 0) out += ", ";
                        append_ast_type_name(out, f->params[i]);
                    }
                    out += ") -> ";
                    append_ast_type_name(out, f->ret);
                    break;
                }
                case ast::TypeKind::Tuple: {
                    auto *tt = static_cast<const ast::TupleType *>(t);
                    out += '(';
                    for (std::size_t i = 0; i < tt->elems.size(); ++i) {
                        if (i > 0) out += ", ";
                        append_ast_type_name(out, tt->elems[i]);
                    }
                    out += ')';
                    break;
                }
                case ast::TypeKind::Applied: {
                    auto *ap = static_cast<const ast::AppliedType *>(t);
                    append_ast_type_name(out, ap->base);
                    if (!ap->args.empty()) {
                        out += '<';
                        for (std::size_t i = 0; i < ap->args.size(); ++i) {
                            if (i > 0) out += ", ";
                            append_ast_type_name(out, ap->args[i]);
                        }
                        out += '>';
                    }
                    break;
                }
            }
        }

        void append_fn_signature(std::pmr::string &out, const ast::FnDecl *fn) {
            out += '(';
            for (std::size_t i = 0; i < fn->params.size(); ++i) {
                if (i > 0) out += ", ";
                append_ast_type_name(out, fn->params[i]->type_);
            }
            out += ") -> ";
            if (fn->return_type) append_ast_type_name(out, fn->return_type);
            else out += "Void";
        }

        const ast::InterfaceDecl *get_interface_from_type(const Checker *c, const ast::Type *t) {
            if (!t) return nullptr;
            if (t->kind != ast::TypeKind::Named) return nullptr;
            auto *nt = static_cast<const ast::NamedType *>(t);
            auto *sym = resolver::lookup(c->resolver_, nt);
            if (!sym || sym->kind != resolver::SymbolKind::Interface) return nullptr;
            return sym->decl.interface_;
        }

        void check_interface_decl(Checker *c, const ast::InterfaceDecl *id) {
            if (!id->type_params.empty()) {
                diagnostics::report_error(c->diag, id->name.span,
                                          "generic interfaces not supported in v1");
            }
            for (auto *base: id->bases) {
                diagnostics::report_error(c->diag, base->span,
                                          "interface inheritance not supported in v1 "
                                          "(list each parent interface on the implementing class)");
            }
            for (auto *m: id->methods) {
                if (m->body) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "interface method '{}' cannot have a body", m->name.text);
                }
                if (m->is_static) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "interface method '{}' cannot be static", m->name.text);
                }
                if (!m->type_params.empty()) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "interface method '{}' cannot be generic", m->name.text);
                }
                if (m->name.text == frappe::syntax::INIT_NAME) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "'init' cannot appear in an interface");
                }
                if (m->name.text == frappe::syntax::DEINIT_NAME) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "'deinit' cannot appear in an interface");
                }
            }
        }

        void check_implements(Checker *c, const ast::ClassDecl *cd) {
            std::pmr::unordered_set<const ast::InterfaceDecl *> own_seen{c->mr};
            for (auto *iface_ty: cd->interfaces) {
                if (iface_ty->kind == ast::TypeKind::Applied) {
                    diagnostics::report_error(c->diag, iface_ty->span,
                                              "generic interfaces not supported in v1");
                    continue;
                }
                auto *iface = get_interface_from_type(c, iface_ty);
                if (!iface) {
                    std::pmr::string name{c->mr};
                    append_ast_type_name(name, iface_ty);
                    diagnostics::report_error(c->diag, iface_ty->span,
                                              "'{}' is not an interface", name);
                    continue;
                }
                if (own_seen.contains(iface)) {
                    diagnostics::report_error(c->diag, iface_ty->span,
                                              "class '{}' implements interface '{}' more than once",
                                              cd->name.text, iface->name.text);
                    continue;
                }
                own_seen.insert(iface);
            }
            if (cd->is_abstract) return;
            // collect every interface from the inheritance chain, deduped
            std::pmr::vector<const ast::InterfaceDecl *> all_ifaces{c->mr};
            std::pmr::unordered_set<const ast::InterfaceDecl *> seen{c->mr};
            for (const ast::ClassDecl *walk = cd; walk; walk = get_base_class(c, walk)) {
                for (auto *iface_ty: walk->interfaces) {
                    auto *iface = get_interface_from_type(c, iface_ty);
                    if (!iface) continue;
                    if (seen.contains(iface)) continue;
                    seen.insert(iface);
                    all_ifaces.push_back(iface);
                }
            }
            for (auto *iface: all_ifaces) {
                for (auto *req: iface->methods) {
                    auto *impl = find_method(c, cd, req->name.text);
                    if (!impl) {
                        diagnostics::report_error(c->diag, cd->name.span,
                                                  "class '{}' does not implement interface '{}' method '{}'",
                                                  cd->name.text, iface->name.text, req->name.text);
                    } else if (!signatures_match(req, impl)) {
                        std::pmr::string expected{c->mr}, got{c->mr};
                        append_fn_signature(expected, req);
                        append_fn_signature(got, impl);
                        diagnostics::report_error(c->diag, impl->name.span,
                                                  "class '{}' method '{}' has incompatible signature "
                                                  "with interface '{}'; expected '{}', got '{}'",
                                                  cd->name.text, req->name.text, iface->name.text,
                                                  expected, got);
                    }
                }
            }
        }

        void check_var_decl(Checker *c, const ast::VarDecl *vd) {
            if (vd->type_) {
                auto *declared = resolve_ast_type(c, vd->type_);
                if (vd->init) {
                    auto *it = check_expr_against(c, vd->init, declared);
                    check_assignable(c, declared, it, vd->init->span);
                }
                if (auto *sym = resolver::lookup(c->resolver_, vd); sym) {
                    c->symbol_types[sym] = declared;
                }
            } else if (vd->init) {
                auto *it = check_expr(c, vd->init);
                if (auto *sym = resolver::lookup(c->resolver_, vd); sym) {
                    c->symbol_types[sym] = it;
                }
            }
        }

        void check_class_decl(Checker *c, const ast::ClassDecl *cd) {
            for (auto *field: cd->fields) {
                if (field->type_) {
                    auto *ft = resolve_ast_type(c, field->type_);
                    if (field->init) {
                        auto *it = check_expr(c, field->init);
                        check_assignable(c, ft, it, field->init->span);
                    }
                    if (auto *sym = resolver::lookup(c->resolver_, field); sym) {
                        c->symbol_types[sym] = ft;
                    }
                } else if (field->init) {
                    auto *it = check_expr(c, field->init);
                    if (auto *sym = resolver::lookup(c->resolver_, field); sym) {
                        c->symbol_types[sym] = it;
                    }
                }
            }
            for (auto *method: cd->methods) {
                if (method->name.text == syntax::INIT_NAME) {
                    if (method->return_type) {
                        diagnostics::report_error(c->diag, method->return_type->span,
                                                  "'init' cannot declare a return type");
                    }
                    if (method->is_static) {
                        diagnostics::report_error(c->diag, method->name.span,
                                                  "'init' cannot be static");
                    }
                    if (!method->type_params.empty()) {
                        diagnostics::report_error(c->diag, method->name.span,
                                                  "'init' cannot be generic");
                    }
                }
                if (method->name.text == syntax::DEINIT_NAME) {
                    if (method->return_type) {
                        diagnostics::report_error(c->diag, method->return_type->span,
                                                  "'deinit' cannot declare a return type");
                    }
                    if (method->is_static) {
                        diagnostics::report_error(c->diag, method->name.span,
                                                  "'deinit' cannot be static");
                    }
                    if (!method->type_params.empty()) {
                        diagnostics::report_error(c->diag, method->name.span,
                                                  "'deinit' cannot be generic");
                    }
                    if (!method->params.empty()) {
                        diagnostics::report_error(c->diag, method->name.span,
                                                  "'deinit' must take no parameters, got {}",
                                                  method->params.size());
                    }
                }
            }
            // Rule 1: Final class cannot be extended
            if (cd->base && cd->base->kind == ast::TypeKind::Named) {
                auto *bnt = static_cast<const ast::NamedType *>(cd->base);
                auto *base_sym = resolver::lookup(c->resolver_, bnt);
                if (base_sym && base_sym->kind == resolver::SymbolKind::Class
                    && base_sym->decl.class_ && base_sym->decl.class_->is_final) {
                    diagnostics::report_error(c->diag, cd->base->span,
                                              "cannot extend final class '{}'", base_sym->name);
                }
            }

            for (auto *m: cd->methods) {
                // Rule 2: cannot override a parent's final method.
                // Also catches: override with no parent, override with no matching parent.
                if (m->is_override) {
                    const ast::ClassDecl *base_cd = get_base_class(c, cd);
                    if (!base_cd) {
                        diagnostics::report_error(c->diag, m->name.span,
                                                  "'override' method '{}' has no parent class", m->name.text);
                    } else {
                        auto *parent = find_method(c, base_cd, m->name.text);
                        if (!parent) {
                            diagnostics::report_error(c->diag, m->name.span,
                                                      "'override' method '{}' not found in any base class",
                                                      m->name.text);
                        } else if (parent->is_final) {
                            diagnostics::report_error(c->diag, m->name.span,
                                                      "cannot override final method '{}'", m->name.text);
                        } else if (!signatures_match(parent, m)) {
                            std::pmr::string expected{c->mr}, got{c->mr};
                            append_fn_signature(expected, parent);
                            append_fn_signature(got, m);
                            diagnostics::report_error(c->diag, m->name.span,
                                                      "override method '{}' has incompatible signature; "
                                                      "expected '{}', got '{}'",
                                                      m->name.text, expected, got);
                        }
                    }
                }

                // Rule 3: final + abstract on the same method is contradictory
                if (m->is_final && m->is_abstract) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "method '{}' cannot be both 'final' and 'abstract'", m->name.text);
                }
            }
            // Rule 4b: a final field with no declaration-site intializer needs a
            // class-local 'init' to set it. Without either the field can never be
            // assigned a dead final field
            bool class_has_own_init = false;
            for (auto *m: cd->methods) {
                if (m->name.text == frappe::syntax::INIT_NAME) {
                    class_has_own_init = true;
                    break;
                }
            }
            if (!class_has_own_init) {
                for (auto *field: cd->fields) {
                    if (!field->is_final) continue;
                    if (field->init != nullptr) continue; // has `= expr`
                    diagnostics::report_error(c->diag, field->name.span,
                                              "final field '{}' has no initializer and class has no 'init'",
                                              field->name.text);
                }
            }
            for (auto *m: cd->methods) {
                if (m->is_static && m->is_abstract) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "method '{}' cannot be both 'static' and 'abstract'", m->name.text);
                }
                if (m->is_static && m->is_override) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "method '{}' cannot be both 'static' and 'override'", m->name.text);
                }
            }

            for (auto *m: cd->methods) {
                // Rule 2: abstract fn requires abstract class.
                if (m->is_abstract && !cd->is_abstract) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "abstract method '{}' requires abstract class", m->name.text);
                }

                // Rule 3: abstract fn cannot have a body.
                if (m->is_abstract && m->body != nullptr) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "abstract method '{}' cannot have a body", m->name.text);
                }

                // Rule 3b: init/deinit cannot be abstract.
                if (m->is_abstract && m->name.text == frappe::syntax::INIT_NAME) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "'init' cannot be abstract");
                }
                if (m->is_abstract && m->name.text == frappe::syntax::DEINIT_NAME) {
                    diagnostics::report_error(c->diag, m->name.span,
                                              "'deinit' cannot be abstract");
                }
            }
            check_implements(c,cd);
            if (!cd->is_abstract && cd->base) {
                // Build the inheritance chain root-first: [root, ..., cd].
                std::pmr::vector<const ast::ClassDecl *> chain{c->mr};
                for (const ast::ClassDecl *w = cd; w; w = get_base_class(c, w)) {
                    chain.push_back(w);
                }
                // Reverse in place — pmr::vector is bidirectional, use std::reverse.
                for (std::size_t i = 0, j = chain.size(); i + 1 < j; ++i, --j) {
                    std::swap(chain[i], chain[j - 1]);
                }

                // Build name→concrete-impl map. Later entries (more-derived)
                // override earlier ones, so cd's own methods win over ancestors'.
                std::pmr::unordered_map<std::string_view, const ast::FnDecl *>
                        concrete{c->mr};
                for (auto *cls: chain) {
                    for (auto *m: cls->methods) {
                        if (!m->is_abstract) concrete[m->name.text] = m;
                    }
                }

                // Walk abstract ancestors; report each unsatisfied one.
                std::pmr::unordered_set<std::string_view> reported{c->mr};
                for (auto *ancestor: chain) {
                    if (ancestor == cd) continue;
                    for (auto *m: ancestor->methods) {
                        if (!m->is_abstract) continue;
                        if (reported.contains(m->name.text)) continue;
                        reported.insert(m->name.text);

                        auto it = concrete.find(m->name.text);
                        if (it == concrete.end()) {
                            diagnostics::report_error(c->diag, cd->name.span,
                                                      "concrete class '{}' must implement abstract method '{}'",
                                                      cd->name.text, m->name.text);
                        } else if (!signatures_match(m, it->second)) {
                            std::pmr::string expected{c->mr}, got{c->mr};
                            append_fn_signature(expected, m);
                            append_fn_signature(got, it->second);
                            diagnostics::report_error(c->diag, it->second->name.span,
                                                      "concrete class '{}' method '{}' does not match "
                                                      "abstract method in '{}'; expected '{}', got '{}'",
                                                      cd->name.text, m->name.text, ancestor->name.text,
                                                      expected, got);
                        }
                    }
                }
            }


            // v1 limitation: we don't verify that `init` actually assigns the field
            // on every path. A class with `init` that forgets a final field is not
            // diagnosed. Full "definitely assigned" checking needs dataflow analysis;
            // defer to v2


            auto *old_class = c->current_class;
            c->current_class = cd;
            for (auto *method: cd->methods) {
                if (!method->body) continue;
                if (!method->type_params.empty() && c->subst == nullptr) {
                    continue; // generic method — deferred
                }
                check_fn_decl(c, method);
            }
            c->current_class = old_class;
        }
    } // namespace
    void check_fn_decl(Checker *c, const ast::FnDecl *fn) {
        for (auto *param: fn->params) {
            if (param->type_) {
                if (auto *sym = resolver::lookup(c->resolver_, param); sym) {
                    c->symbol_types[sym] = resolve_ast_type(c, param->type_);
                }
            }
        }
        if (fn->return_type) static_cast<void>(resolve_ast_type(c, fn->return_type));
        if (!fn->body) return;

        auto *old = c->current_fn;
        c->current_fn = fn;
        for (auto *stmt: fn->body->stmts) check_stmt(c, stmt);
        c->current_fn = old;
    }

    void check_decl(Checker *c, const ast::Decl *d) {
        if (!d) return;
        switch (d->kind) {
            case ast::DeclKind::Fn: {
                auto *fn = static_cast<const ast::FnDecl *>(d);
                if (!fn->type_params.empty() && c->subst == nullptr) {
                    return; // generic template - deferred until instantiation
                }
                check_fn_decl(c, fn);
                break;
            }
            case ast::DeclKind::Var: check_var_decl(c, static_cast<const ast::VarDecl *>(d));
                break;
            case ast::DeclKind::Class: check_class_decl(c, static_cast<const ast::ClassDecl *>(d));
                break;
            case ast::DeclKind::Interface:
                check_interface_decl(c, static_cast<const  ast::InterfaceDecl *>(d));
                break;
            case ast::DeclKind::Enum:
            case ast::DeclKind::Import:
                break;
        }
    }
} // namespace frappe::checker::detail
