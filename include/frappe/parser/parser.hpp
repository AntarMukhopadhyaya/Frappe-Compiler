//
// Created by antar on 19/10/26.
//

#pragma once

#include <cstddef>
#include <memory_resource>
#include <span>
#include <frappe/ast/program.hpp>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/syntax/token.hpp>

namespace frappe::parser {
    struct Parser {
        std::span<const syntax::Token> tokens;
        std::size_t current = 0;
        std::pmr::memory_resource* mr = nullptr;
        diagnostics::Engine* diag = nullptr;

    };
    [[nodiscard]] ast::Program parse(
    std::span<const syntax::Token>  tokens,
    diagnostics::Engine*            diag,
    std::pmr::memory_resource*      mr);
}