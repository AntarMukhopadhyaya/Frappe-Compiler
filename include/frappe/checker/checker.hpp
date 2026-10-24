//
// Created by antar on 19/10/26.
//

#pragma once


#include <memory_resource>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/program.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/resolver/resolver.hpp>
#include <frappe/types/store.hpp>
#include <frappe/types/types.hpp>

namespace frappe::checker {
    struct Subst {
        std::pmr::unordered_map<std::string_view, types::Type *> bindings;
    };

    // Groups type-related caches into one scope, simplifying cache management
    // and context save/restore. Currently used for generic bodies; can also
    // support per-function or per-block scopes.
    struct Scope {
        std::pmr::unordered_map<const void *, types::Type *> node_types;
        std::pmr::unordered_map<const resolver::Symbol *, types::Type *> symbol_types;
        std::pmr::unordered_map<const void *, std::pmr::string> call_names;
    };

    // One monomorphic instantiation of a generic function
    struct Checked_Fn {
        std::pmr::string name;
        const ast::FnDecl *decl; // the template
        std::pmr::vector<types::Type *> type_args;
        std::pmr::vector<types::Type *> param_types;
        types::Type *return_type;


        // Per-instance caches. Pushed onto Checker::scope_stack during the
        // instance's body check (via Context_Guard); popped afterwards.
        // Data survives the pop so MIR builder / codegen can read it.
        Scope scope;
    };

    // A queued instantiation whose body hasn't been checked yet
    struct Pending_Instance {
        const ast::FnDecl *decl;
        Subst *subst;
        Checked_Fn *inst; // so check_pending can redirect node_types
    };

    struct Checker {
        std::pmr::memory_resource *mr;
        diagnostics::Engine *diag;
        resolver::Resolver *resolver_;
        types::Store store;

        std::pmr::unordered_map<const void *, types::Type *> node_types;
        std::pmr::unordered_map<const resolver::Symbol *, types::Type *> symbol_types;
        std::pmr::unordered_map<const void *, std::pmr::string> call_names;

        std::pmr::unordered_set<const resolver::Symbol *> visiting;

        const ast::FnDecl *current_fn = nullptr;
        const ast::ClassDecl *current_class = nullptr;
        Subst *subst = nullptr;
        std::pmr::unordered_map<std::pmr::string, Checked_Fn *> instances;
        std::pmr::vector<Pending_Instance> worklist;
        int loop_depth = 0;
        // Active scopes, innermost last. Empty = only the globals are
        // visible. Non-empty only while checking a generic instance's body
        // (see Context_Guard in generic.cpp). Lookups walk from back to
        // front, then fall through to the globals; writes go to back().
        std::pmr::vector<Scope *> scope_stack;
    };

    [[nodiscard]] Checker make_checker(resolver::Resolver *res,
                                       diagnostics::Engine *diag,
                                       std::pmr::memory_resource *mr);

    void check_program(Checker *c, const ast::Program &program);

    inline void set_type(Checker *c, const void *node, types::Type *t) {
        if (c->scope_stack.empty()) {
            c->node_types.insert_or_assign(node, t);
        } else {
            c->scope_stack.back()->node_types.insert_or_assign(node, t);
        }
    }

    // Reads the active cache first (per-instance if inside a generic body),
    // then falls back to the global cache. Symmetric with set_type.
    inline void set_symbol_type(Checker *c, const resolver::Symbol *sym, types::Type *t) {
        if (c->scope_stack.empty()) {
            c->symbol_types.insert_or_assign(sym, t);
        } else {
            c->scope_stack.back()->symbol_types.insert_or_assign(sym, t);
        }
    }

    inline void record_call_name(Checker *c, const void *node, std::pmr::string name) {
        if (c->scope_stack.empty()) {
            c->call_names.insert_or_assign(node, std::move(name));
        } else {
            c->scope_stack.back()->call_names.insert_or_assign(node, std::move(name));
        }
    }

    [[nodiscard]] inline types::Type *get_type(Checker *c, const void *node) {
        for (auto &it: std::views::reverse(c->scope_stack)) {
            if (auto found = it->node_types.find(node);
                found != it->node_types.end()) {
                return found->second;
            }
        }
        auto it = c->node_types.find(node);
        return it != c->node_types.end() ? it->second : nullptr;
    }

    // Public type resolver for MIR Builder and downstream phases.
    [[nodiscard]] types::Type *resolve_type(Checker *c, const ast::Type *t);
}
