//
// Created by antar on 19/10/26.
//
#pragma once

#include <memory_resource>
#include <vector>
#include <frappe/ast/node.hpp>
#include <frappe/ast/name.hpp>


namespace frappe::ast {
    struct NamedType : Type {
        Name name;
    };
    struct ArrayType : Type {
        Type* element = nullptr;
        std::int64_t size = -1; // -1 dynamic
    };
    struct OptionalType : Type {
        Type* inner = nullptr;
    };
    struct  AppliedType : Type {
        Type* base = nullptr;
        std::pmr::vector<Type*> args;
    };
    struct FuncType : Type {
        std::pmr::vector<Type*> params;
        Type* ret = nullptr;
    };
    struct TupleType : Type {
        std::pmr::vector<Type*> elems;
    };
}