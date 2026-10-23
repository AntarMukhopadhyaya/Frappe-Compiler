//
// Created by antar on 19/10/26.
//

#pragma once
#include <cstdint>
#include <memory_resource>
#include <string_view>
#include <unordered_map>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/program.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/resolver/builtin.hpp>
#include <frappe/source/span.hpp>


namespace frappe::resolver {
    enum class SymbolKind : std::uint8_t {
        Module, Var, LocalVar, ForInVar, Param, Fn, Class, Interface, Enum,
        Field, Method, TypeParam, This, Super, BuiltinType
    };

    enum class ScopeKind : std::uint8_t {
        Module, Function, Block, Class, Enum, Loop, TypeParam,
    };

    union DeclRef {
        const ast::FnDecl *fn;
        const ast::ClassDecl *class_;
        const ast::InterfaceDecl *interface_;
        const ast::EnumDecl *enum_;
        const ast::VarDecl *var_decl;
        const ast::ImportDecl *import_;
        const ast::Param *param;
        const ast::FieldDecl *field;
        const ast::EnumVariant *enum_variant;
        const ast::TypeParam *type_param;
        const ast::VarStmt *var_stmt;
        const ast::ForInStmt *for_in;
    };

    struct Symbol {
        std::string_view name;
        SymbolKind kind;
        DeclRef decl = {};
        Symbol *parent = nullptr;
        Symbol *base = nullptr;
        Builtin builtin = Builtin::Int;
    };

    struct Scope {
        Scope *parent = nullptr;
        ScopeKind kind = ScopeKind::Module;
        std::pmr::unordered_map<std::string_view, Symbol *> symbols;
    };

    struct Resolver {
        std::pmr::memory_resource *mr;
        diagnostics::Engine *diag;
        Scope *current = nullptr;
        Scope *module_scope = nullptr;
        std::pmr::unordered_map<const void *, Symbol *> bindings;
        Symbol *current_class = nullptr;
        std::pmr::unordered_map<const ast::FnDecl*, Symbol*> this_syms;
        std::pmr::unordered_map<const ast::FnDecl*, Symbol*> super_syms;
    };

    [[nodiscard]] Resolver make_resolver(diagnostics::Engine *diag,
                                         std::pmr::memory_resource *mr);

    Scope *push_scope(Resolver *r, ScopeKind kind);

    void pop_scope(Resolver *r);

    [[nodiscard]] Symbol *declare(Resolver *r, std::string_view name,
                                  SymbolKind kind, DeclRef decl, source::Span span);

    Symbol *declare_synthetic(Resolver *r, std::string_view name,
                              SymbolKind kind, Symbol *parent);

    [[nodiscard]]
    Symbol *resolve(Resolver *r, std::string_view name);

    // `bind` associates an AST node (identified by its concrete pointer) with a
    // symbol. Any node address — Expr*, Stmt*, Decl*, Param*, etc. — is valid.
    void bind(Resolver *r, const void *node, Symbol *sym);

    [[nodiscard]] Symbol *lookup(Resolver *r, const void *node);

    [[nodiscard]] Symbol *class_base(Symbol *class_sym);

    void resolve_program(Resolver *r, const ast::Program &program);
}
