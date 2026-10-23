//
// Created by antar on 19/10/26.
//

#pragma once


#include <cstdint>
#include <memory_resource>
#include <vector>

#include <frappe/ast/node.hpp>
#include <frappe/ast/name.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>

namespace frappe::ast {
    enum class Visibility : std::uint8_t {
        Default, Private, Public, Protected
    };

    struct Param {
        source::Span span;
        Name name;
        Type *type_ = nullptr;
    };

    struct TypeParam {
        source::Span span;
        Name name;
        Type *bound = nullptr; // nil = unconstrained
    };

    struct FieldDecl {
        source::Span span;
        Name name;
        Type *type_ = nullptr;
        Expr *init = nullptr;
        Visibility visibility = Visibility::Default;
        bool is_static = false;
        bool is_final = false;
    };

    struct EnumVariant {
        source::Span span;
        Name name;
        Expr *value = nullptr; // nil → auto-assigned
    };

    struct ImportDecl : Decl {
        Name path;
        Name alias;
        bool has_alias = false;
    };

    struct FnDecl : Decl {
        Name name;
        std::pmr::vector<TypeParam *> type_params;
        std::pmr::vector<Param *> params;
        Type *return_type = nullptr;
        BlockStmt *body = nullptr;
        Visibility visibility = Visibility::Default;
        bool is_static = false;
        bool is_final = false;
        bool is_override = false;
        bool is_abstract = false;
        bool is_constructor = false;
        bool is_destructor = false;
    };

    struct ClassDecl : Decl {
        Name name;
        std::pmr::vector<TypeParam *> type_params;
        Type *base = nullptr;
        std::pmr::vector<Type *> interfaces;
        std::pmr::vector<FieldDecl *> fields;
        std::pmr::vector<FnDecl *> methods;
        Visibility visibility = Visibility::Default;
        bool is_final = false;
        bool is_abstract = false;
    };

    struct InterfaceDecl : Decl {
        Name name;
        std::pmr::vector<TypeParam *> type_params;
        std::pmr::vector<Type *> bases;
        std::pmr::vector<FnDecl *> methods;
        Visibility visibility = Visibility::Default;
    };

    struct EnumDecl : Decl {
        Name name;
        std::pmr::vector<EnumVariant *> variants;
        Visibility visibility = Visibility::Default;
    };

    struct VarDecl : Decl {
        Name name;
        Type *type_ = nullptr;
        Expr *init = nullptr;
        Visibility visibility = Visibility::Default;
        bool is_mutable = false;
    };
}
