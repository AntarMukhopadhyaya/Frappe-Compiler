//
// Created by antar on 19/10/26.
//

#pragma once


#include <optional>
#include <frappe/ast/operator.hpp>
#include <frappe/syntax/token_kind.hpp>


namespace frappe::parser {
    [[nodiscard]] constexpr std::optional<ast::BinaryOp>
to_binary_op(syntax::TokenKind k) noexcept {
        if (k == syntax::TokenKind::Plus)  return ast::BinaryOp::Add;
        if (k == syntax::TokenKind::Minus) return ast::BinaryOp::Sub;
        if (k == syntax::TokenKind::Star)  return ast::BinaryOp::Mul;
        if (k == syntax::TokenKind::Slash) return ast::BinaryOp::Div;
        if (k == syntax::TokenKind::Mod)   return ast::BinaryOp::Mod;
        if (k == syntax::TokenKind::EqEq)  return ast::BinaryOp::Eq;
        if (k == syntax::TokenKind::Neq)   return ast::BinaryOp::Neq;
        if (k == syntax::TokenKind::Lt)    return ast::BinaryOp::Lt;
        if (k == syntax::TokenKind::Lte)   return ast::BinaryOp::Lte;
        if (k == syntax::TokenKind::Gt)    return ast::BinaryOp::Gt;
        if (k == syntax::TokenKind::Gte)   return ast::BinaryOp::Gte;
        if (k == syntax::TokenKind::And)   return ast::BinaryOp::And;
        if (k == syntax::TokenKind::Or)    return ast::BinaryOp::Or;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<ast::UnaryOp>
 to_unary_op(syntax::TokenKind k) noexcept {
        if (k == syntax::TokenKind::Minus) return ast::UnaryOp::Neg;
        if (k == syntax::TokenKind::Not)   return ast::UnaryOp::Not;
        return std::nullopt;
    }

    [[nodiscard]] constexpr std::optional<ast::AssignOp>
    to_assign_op(syntax::TokenKind k) noexcept {
        if (k == syntax::TokenKind::Eq)      return ast::AssignOp::Assign;
        if (k == syntax::TokenKind::PlusEq)  return ast::AssignOp::AddAssign;
        if (k == syntax::TokenKind::MinusEq) return ast::AssignOp::SubAssign;
        if (k == syntax::TokenKind::StarEq)  return ast::AssignOp::MulAssign;
        if (k == syntax::TokenKind::SlashEq) return ast::AssignOp::DivAssign;
        if (k == syntax::TokenKind::ModEq)   return ast::AssignOp::ModAssign;
        return std::nullopt;
    }
}
