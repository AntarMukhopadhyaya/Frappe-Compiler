//
// Created by antar on 19/10/26.
//

#pragma once

#include <cstdint>

namespace  frappe::ast {
    enum class DeclKind : std::uint8_t {
        Import, Fn, Class, Interface, Enum, Var
    };
    enum class StmtKind : std::uint8_t {
        Expr, Var, Block, If, While, For, ForIn,
        Switch, Break, Continue, Return, Defer, Panic
    };

    enum class ExprKind : std::uint8_t {
        Int, Float, String, Bool, Nil,
        Ident, This, Super,
        Binary, Unary, Assign,
        Call, Member, Index, New,
        Group, Array, Tuple,
    };

    enum class TypeKind : std::uint8_t {
        Named, Array, Optional, Applied, Func, Tuple,
    };
}