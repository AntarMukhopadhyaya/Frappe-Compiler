//
// Created by antar on 19/10/26.
//

#pragma once

#include <cstdint>
#include <memory_resource>
#include <string_view>
#include <vector>
#include <frappe/ast/node.hpp>
#include <frappe/ast/name.hpp>
#include <frappe/ast/operator.hpp>
#include <frappe/ast/type.hpp>


namespace frappe::ast {
    struct IntExpr : Expr {
        std::int64_t value = 0;
    };

    struct FloatExpr : Expr {
        double value = 0.0;
    };

    struct StringExpr : Expr {
        std::string_view value;
    };

    struct BoolExpr : Expr {
        bool value = false;
    };

    struct NilExpr : Expr {
    };

    struct IdentExpr : Expr {
        Name name;
    };

    struct ThisExpr : Expr {
    };

    struct SuperExpr : Expr {
    };

    struct BinaryExpr : Expr {
        Expr *left = nullptr;
        BinaryOp op = BinaryOp::Add;
        source::Span op_span;
        Expr *right = nullptr;
    };

    struct UnaryExpr : Expr {
        UnaryOp op = UnaryOp::Neg;
        source::Span op_span;
        Expr *operand = nullptr;
    };

    struct AssignExpr : Expr {
        Expr *left = nullptr;
        AssignOp op = AssignOp::Assign;
        source::Span op_span;
        Expr *right = nullptr;
    };

    struct CallExpr : Expr {
        Expr *callee = nullptr;
        std::pmr::vector<Expr *> args;
    };

    struct MemberExpr : Expr {
        Expr *object = nullptr;
        Name name;
    };

    struct IndexExpr : Expr {
        Expr *object = nullptr;
        Expr *index = nullptr;
    };

    struct NewExpr : Expr {
        Type *type_ = nullptr;
        std::pmr::vector<Expr *> args;
    };

    struct GroupExpr : Expr {
        Expr *expr = nullptr;
    };

    struct ArrayExpr : Expr {
        std::pmr::vector<Expr *> elements;
    };

    struct TupleExpr : Expr {
        std::pmr::vector<Expr *> elements;
    };

}
