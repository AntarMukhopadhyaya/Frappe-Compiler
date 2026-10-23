//
// Created by antar on 19/10/26.
//

#pragma  once
#include <type_traits>

#include <frappe/source/span.hpp>
#include <frappe/ast/kinds.hpp>

namespace frappe::ast {
    struct Decl {
        DeclKind kind;
        source::Span span;
    };
    struct Stmt {
        StmtKind kind;
        source::Span span;
    };
    struct Expr {
        ExprKind kind;
        source::Span span;
    };
    struct Type {
        TypeKind kind;
        source::Span span;
    };
}