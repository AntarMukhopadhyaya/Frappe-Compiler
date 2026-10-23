//
// Created by antar on 19/10/26.
//

#pragma once


#include <cstddef>
#include <cstdint>
#include <format>
#include <memory_resource>
#include <span>
#include <string_view>
#include <vector>

#include <frappe/diagnostics/engine.hpp>
#include <frappe/source/span.hpp>
#include <frappe/syntax/token.hpp>
#include <frappe/syntax/keywords.hpp>
#include <frappe/syntax/token_kind.hpp>


namespace frappe::lexer {
    struct Lexer {
        std::string_view source;
        std::pmr::vector<syntax::Token> tokens;
        diagnostics::Engine *diag = nullptr;
        std::size_t start = 0;
        std::size_t current = 0;
        std::uint32_t line = 1;
        std::uint32_t column = 1;
        std::uint32_t token_line = 1;
        std::uint32_t token_column = 1;
    };

    [[nodiscard]] Lexer make_lexer(
        std::string_view source,
        diagnostics::Engine *diag,
        std::pmr::memory_resource *mr);

    // Tokenizes the source, stores tokens in `l.tokens`, and returns a
    // non-owning view into that storage.
    //
    // The caller must keep `l` and its memory resource alive, and must not
    // perform operations that invalidate the token storage while the span
    // is in use.
    [[nodiscard]] std::span<const syntax::Token> tokenize(Lexer* l);

    [[nodiscard]] constexpr bool is_alpha(char c) noexcept {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    [[nodiscard]] constexpr bool is_digit(char c) noexcept {
        return c >= '0' && c <= '9';
    }
    [[nodiscard]] constexpr bool is_ident_char(char c) noexcept {
        return is_alpha(c) || is_digit(c) || c == '_';
    }
}
