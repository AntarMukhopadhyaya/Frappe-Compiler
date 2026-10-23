//
// Created by antar on 19/10/26.
//

#pragma once


#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>


#include <frappe/ast/decl.hpp>
#include <frappe/resolver/builtin.hpp>
#include <frappe/types/kinds.hpp>


namespace frappe::types {
    struct Type {
        TypeKind kind;
    };

    struct BuiltinType : Type {
        resolver::Builtin b;
    };

    enum class NamedDeclKind : std::uint8_t {
        Class,
        Interface,
        Enum,
    };

    struct NamedTypeDeclRef {
        NamedDeclKind kind;

        union {
            const ast::ClassDecl *class_;
            const ast::InterfaceDecl *interface_;
            const ast::EnumDecl *enum_;
        };
    };

    struct NamedType : Type {
        NamedTypeDeclRef decl;
        std::pmr::vector<Type *> args;
    };

    struct ArrayType : Type {
        Type *element;
        int64_t size; // -1 = dynamic
    };

    struct OptionalType : Type {
        Type *inner;
    };

    struct FuncType : Type {
        std::pmr::vector<Type *> params;
        Type *ret;
    };

    struct TupleType : Type {
        std::pmr::vector<Type *> elems;
    };

    struct TypeVar : Type {
        std::string_view name;
        const ast::Decl *owner;
    };

    struct Unknown : Type {
    };

    struct Error : Type {
    };

    [[nodiscard]] inline bool is_error(const Type *t) noexcept {
        return t != nullptr && t->kind == TypeKind::Error;
    }

    [[nodiscard]] inline bool is_unknown(const Type *t) noexcept {
        return t != nullptr && t->kind == TypeKind::Unknown;
    }

    [[nodiscard]] inline const void *named_decl_ptr(const NamedTypeDeclRef &r) noexcept {
        switch (r.kind) {
            case NamedDeclKind::Class: return r.class_;
            case NamedDeclKind::Interface: return r.interface_;
            case NamedDeclKind::Enum: return r.enum_;
        }
        return nullptr;
    }

    [[nodiscard]] inline std::string_view named_decl_name(const NamedTypeDeclRef &r) noexcept {
        switch (r.kind) {
            case NamedDeclKind::Class: return r.class_->name.text;
            case NamedDeclKind::Interface: return r.interface_->name.text;
            case NamedDeclKind::Enum: return r.enum_->name.text;
        }
        return "<type>";
    }

    [[nodiscard]] bool type_eq(const Type *a, const Type *b);

    void write_type_name(std::pmr::string &out, const Type *t);

    [[nodiscard]] std::pmr::string type_name(std::pmr::memory_resource *mr, const Type *t);
}
