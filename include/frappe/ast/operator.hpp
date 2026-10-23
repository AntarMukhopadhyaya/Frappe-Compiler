//
// Created by antar on 19/10/26.
//

#pragma once

#include <cstdint>
#include <string_view>

namespace frappe::ast {
    enum class BinaryOp : std::uint8_t {
        Add, Sub, Mul, Div, Mod,
        Eq, Neq, Lt, Lte, Gt, Gte,
        And, Or,
    };

    enum class UnaryOp : std::uint8_t {
        Neg
        , // -
        Not
        , // !
    };

    enum class AssignOp : std::uint8_t {
        Assign, // =
        AddAssign, // +=
        SubAssign, // -=
        MulAssign, // *=
        DivAssign, // /=
        ModAssign, // %=
    };

    [[nodiscard]] constexpr std::string_view binary_op_name(BinaryOp op) noexcept {
        switch (op) {
            case BinaryOp::Add: return "+";
            case BinaryOp::Sub: return "-";
            case BinaryOp::Mul: return "*";
            case BinaryOp::Div: return "/";
            case BinaryOp::Mod: return "%";
            case BinaryOp::Eq: return "==";
            case BinaryOp::Neq: return "!=";
            case BinaryOp::Lt: return "<";
            case BinaryOp::Lte: return "<=";
            case BinaryOp::Gt: return ">";
            case BinaryOp::Gte: return ">=";
            case BinaryOp::And: return "&&";
            case BinaryOp::Or: return "||";
        }
        return "?";
    }

    [[nodiscard]] constexpr std::string_view unary_op_name(UnaryOp op) noexcept {
        switch (op) {
            case UnaryOp::Neg: return "-";
            case UnaryOp::Not: return "!";
        }
        return "?";
    }

    [[nodiscard]] constexpr std::string_view assign_op_name(AssignOp op) noexcept {
        switch (op) {
            case AssignOp::Assign: return "=";
            case AssignOp::AddAssign: return "+=";
            case AssignOp::SubAssign: return "-=";
            case AssignOp::MulAssign: return "*=";
            case AssignOp::DivAssign: return "/=";
            case AssignOp::ModAssign: return "%=";
        }
        return "?";
    }
}
