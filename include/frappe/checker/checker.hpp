//
// Created by antar on 19/10/26.
//

#pragma once


#include <memory_resource>
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

    // One monomorphic instantiation of a generic function
    struct Checked_Fn {
        std::pmr::string name;
        const ast::FnDecl *decl; // the template
        std::pmr::vector<types::Type *> type_args;
        std::pmr::vector<types::Type *> param_types;
        types::Type *return_type;

        // Captured during instantiation - every AST node inside decl body
        // gets its type here with subst applied per instance so two
        // instantiaion of the same template does not collide
        std::pmr::unordered_map<const void *, types::Type *> node_types;

        std::pmr::unordered_map<const void *, std::pmr::string> call_names;
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
        std::pmr::unordered_set<const resolver::Symbol *> visiting;

        const ast::FnDecl *current_fn = nullptr;
        const ast::ClassDecl *current_class = nullptr;
        Subst *subst = nullptr;
        std::pmr::unordered_map<std::pmr::string, Checked_Fn *> instances;
        std::pmr::vector<Pending_Instance> worklist;


        // If non-null, set_type writes here instead of `node_types`.
        // Non-null only while checking a generic instance's body.
        std::pmr::unordered_map<const void *, types::Type *> *node_types_out = nullptr;
        std::pmr::unordered_map<const void *, std::pmr::string> call_names;
        std::pmr::unordered_map<const void *, std::pmr::string> *call_names_out = nullptr;
    };

    [[nodiscard]] Checker make_checker(resolver::Resolver *res,
                                       diagnostics::Engine *diag,
                                       std::pmr::memory_resource *mr);

    void check_program(Checker *c, const ast::Program &program);

    inline void set_type(Checker *c, const void *node, types::Type *t) {
        if (c->node_types_out) {
            c->node_types_out->insert_or_assign(node, t);
        } else {
            c->node_types.insert_or_assign(node, t);
        }
    }

    [[nodiscard]] inline types::Type *get_type(Checker *c, const void *node) {
        auto it = c->node_types.find(node);
        return it != c->node_types.end() ? it->second : nullptr;
    }

    // Public type resolver for MIR Builder and downstream phases.
    [[nodiscard]] types::Type *resolve_type(Checker *c, const ast::Type *t);
}
