//
// Created by antar on 21/10/26.
//

#include <frappe/checker/checker.hpp>
#include <algorithm>
#include <cstddef>
#include  <string>
#include <string_view>
#include <utility>
#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/type.hpp>

#include "checker_internal.hpp"

namespace frappe::checker::detail {
    namespace {
        using types::Type;
        using types::TypeKind;

        struct Context_Guard {
            Checker *c;
            Subst *saved_subst;
            std::pmr::unordered_map<const void *, types::Type *> *saved_nt;
            std::pmr::unordered_map<const void *, std::pmr::string> *saved_cn;

            Context_Guard(Checker *chk, Subst *subst,
                          std::pmr::unordered_map<const void *, types::Type *> *nt,
                          std::pmr::unordered_map<const void *, std::pmr::string> *cn)
                : c{chk}, saved_subst{chk->subst},
                  saved_nt{chk->node_types_out}, saved_cn{chk->call_names_out} {
                c->subst = subst;
                c->node_types_out = nt;
                c->call_names_out = cn;
            }

            ~Context_Guard() {
                c->subst = saved_subst;
                c->node_types_out = saved_nt;
                c->call_names_out = saved_cn;
            }

            Context_Guard(const Context_Guard &) = delete;

            Context_Guard &operator=(const Context_Guard &) = delete;
        };

        void infer_from_pattern(Checker *c, const ast::Type *pattern,
                                Type *actual, Subst *subst) {
            if (!pattern || !actual) return;
            if (types::is_error(actual)) return;
            switch (pattern->kind) {
                case ast::TypeKind::Named: {
                    auto *nt = static_cast<const ast::NamedType *>(pattern);
                    auto *sym = resolver::lookup(c->resolver_, nt);
                    if (!sym) return;
                    if (sym->kind != resolver::SymbolKind::TypeParam) return;
                    auto it = subst->bindings.find(sym->name);
                    if (it != subst->bindings.end()) {
                        if (!types::type_eq(it->second, actual)) {
                            diagnostics::report_error(c->diag, nt->name.span,
                                                      "type parameter '{}' bound to both {} and {}",
                                                      sym->name,
                                                      types::type_name(c->store.mr, it->second),
                                                      types::type_name(c->store.mr, actual));
                        }
                    } else {
                        subst->bindings.emplace(sym->name, actual);
                    }
                    return;
                }
                case ast::TypeKind::Array: {
                    auto *at = static_cast<const ast::ArrayType *>(pattern);
                    if (actual->kind == TypeKind::Array) {
                        infer_from_pattern(c, at->element,
                                           static_cast<const types::ArrayType *>(actual)->element, subst);
                    }
                    return;
                }
                case ast::TypeKind::Optional: {
                    auto *ot = static_cast<const ast::OptionalType *>(pattern);
                    if (actual->kind == TypeKind::Optional) {
                        infer_from_pattern(c, ot->inner,
                                           static_cast<const types::OptionalType *>(actual)->inner, subst);
                    }
                    return;
                }
                case ast::TypeKind::Tuple: {
                    auto *tt = static_cast<const ast::TupleType *>(pattern);
                    if (actual->kind == TypeKind::Tuple) {
                        auto *at = static_cast<const types::TupleType *>(actual);
                        std::size_t n = std::min(tt->elems.size(), at->elems.size());
                        for (std::size_t i = 0; i < n; ++i) {
                            infer_from_pattern(c, tt->elems[i], at->elems[i], subst);
                        }
                    }
                    return;
                }
                case ast::TypeKind::Func: {
                    auto *ft = static_cast<const ast::FuncType *>(pattern);
                    if (actual->kind == TypeKind::Func) {
                        auto *af = static_cast<const types::FuncType *>(actual);
                        std::size_t n = std::min(ft->params.size(), af->params.size());
                        for (std::size_t i = 0; i < n; ++i) {
                            infer_from_pattern(c, ft->params[i], af->params[i], subst);
                        }
                        if (ft->ret) infer_from_pattern(c, ft->ret, af->ret, subst);
                    }
                    return;
                }
                case ast::TypeKind::Applied: {
                    auto *ap = static_cast<const ast::AppliedType *>(pattern);
                    diagnostics::report_error(c->diag, ap->span,
                                              "generic type in parameter position not supported in v1 inference");
                    return;
                }
            }
        }

        void make_mangled_name(std::pmr::string &out, const ast::FnDecl *fn, Subst *subst) {
            out += fn->name.text;
            for (auto *tp: fn->type_params) {
                out += "$";
                auto it = subst->bindings.find(tp->name.text);
                if (it != subst->bindings.end()) {
                    types::write_type_name(out, it->second);
                }
            }
        }

        Checked_Fn *get_or_create_instance(Checker *c, const ast::FnDecl *fn, Subst *subst) {
            std::pmr::string key{c->mr};
            make_mangled_name(key, fn, subst);
            if (auto it = c->instances.find(key); it != c->instances.end()) {
                return it->second;
            }

            auto *inst = new(c->mr->allocate(sizeof(Checked_Fn), alignof(Checked_Fn)))
                    Checked_Fn{};
            inst->name = std::move(key);
            inst->decl = fn;
            inst->type_args = std::pmr::vector<types::Type *>{c->mr};
            inst->param_types = std::pmr::vector<types::Type *>{c->mr};
            inst->return_type = c->store.void_type;
            inst->node_types = std::pmr::unordered_map<const void *, types::Type *>{c->mr};
            inst->call_names = std::pmr::unordered_map<const void *, std::pmr::string>{c->mr};

            // Resolve params/return under the INSTANCE's context so caching doesn't
            // leak into the global map and poison other instantiations of the same
            // template (identity$String must not see identity$Int's T_node→Int).
            auto *old_subst = c->subst;
            auto *old_nt = c->node_types_out;
            c->subst = subst;
            c->node_types_out = &inst->node_types;

            for (auto *tp: fn->type_params) {
                if (auto it = subst->bindings.find(tp->name.text); it != subst->bindings.end()) {
                    inst->type_args.push_back(it->second);
                } else {
                    inst->type_args.push_back(c->store.error_type);
                }
            }
            for (auto *p: fn->params) {
                inst->param_types.push_back(resolve_ast_type(c, p->type_));
            }
            if (fn->return_type) {
                inst->return_type = resolve_ast_type(c, fn->return_type);
            }

            c->node_types_out = old_nt;
            c->subst = old_subst;

            c->instances.emplace(inst->name, inst);
            c->worklist.push_back(Pending_Instance{fn, subst, inst});

            return inst;
        }

        void check_pending(Checker *c, const Pending_Instance &pending) {
            Context_Guard guard{
                c, pending.subst,
                &pending.inst->node_types,
                &pending.inst->call_names
            };
            check_fn_decl(c, pending.decl);
        }

        Type *bind_expr_for_call(Checker *c, const void *node, Type *t) {
            set_type(c, node, t);
            return t;
        }
    }

    Type *check_generic_call(Checker *c, const ast::CallExpr *call,
                             const ast::FnDecl *fn) {
        if (call->args.size() != fn->params.size()) {
            diagnostics::report_error(c->diag, call->span,
                                      "'{}' expects {} arguments, got {}",
                                      fn->name.text, fn->params.size(), call->args.size());
            for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
            return bind_expr_for_call(c, call, c->store.error_type);
        }
        // Check arg expression once; cache their types
        std::pmr::vector<Type *> arg_types{c->mr};
        arg_types.reserve(call->args.size());
        for (auto *arg: call->args) {
            arg_types.push_back(check_expr(c, arg));
        }
        // Infer type params
        auto *subst = new(c->mr->allocate(sizeof(Subst), alignof(Subst))) Subst{};
        subst->bindings = std::pmr::unordered_map<std::string_view, types::Type *>{c->mr};
        for (std::size_t i = 0; i < fn->params.size(); ++i) {
            infer_from_pattern(c, fn->params[i]->type_, arg_types[i], subst);
        }
        // Every declared type param must be bound
        bool all_bound = true;
        for (auto *tp: fn->type_params) {
            if (!subst->bindings.contains(tp->name.text)) {
                diagnostics::report_error(c->diag, call->span,
                                          "cannot infer type parameter '{}' of '{}'",
                                          tp->name.text, fn->name.text);
                all_bound = false;
            }
        }
        if (!all_bound) return bind_expr_for_call(c, call, c->store.error_type);

        // Look up / create instance
        auto *inst = get_or_create_instance(c, fn, subst);
        record_call_name(c, call, std::pmr::string{inst->name, c->mr});
        //Check args against instantiated param types
        for (std::size_t i = 0; i < call->args.size(); ++i) {
            check_assignable(c, inst->param_types[i], arg_types[i],
                             call->args[i]->span);
        }
        return bind_expr_for_call(c, call, inst->return_type);
    }

    void drain_worklist(Checker *c) {
        while (!c->worklist.empty()) {
            auto pending = c->worklist.back();
            c->worklist.pop_back();
            check_pending(c, pending);
        }
    }
}
