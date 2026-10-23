//
// Created by antar on 19/10/26.
//
#pragma once


#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <frappe/syntax/token_kind.hpp>

namespace frappe::syntax {
    struct Keyword {
        std::string_view name;
        TokenKind kind;
    };

    inline constexpr std::array<Keyword, 36> KEYWORDS = {
        {
            {"fn", TokenKind::Fn},
            {"class", TokenKind::Class},
            {"enum", TokenKind::Enum},
            {"break", TokenKind::Break},
            {"continue", TokenKind::Continue},
            {"return", TokenKind::Return},
            {"if", TokenKind::If},
            {"else", TokenKind::Else},
            {"for", TokenKind::For},
            {"while", TokenKind::While},
            {"switch", TokenKind::Switch},
            {"case", TokenKind::Case},
            {"default", TokenKind::Default},
            {"false", TokenKind::False},
            {"true", TokenKind::True},
            {"nil", TokenKind::Nil},
            {"this", TokenKind::This},
            {"final", TokenKind::Final},
            {"super", TokenKind::Super},
            {"private", TokenKind::Private},
            {"public", TokenKind::Public},
            {"protected", TokenKind::Protected},
            {"static", TokenKind::Static},
            {"override", TokenKind::Override},
            {"extends", TokenKind::Extends},
            {"implements", TokenKind::Implements},
            {"interface", TokenKind::Interface},
            {"abstract", TokenKind::Abstract},
            {"new", TokenKind::New},
            {"let", TokenKind::Let},
            {"as", TokenKind::As},
            {"import", TokenKind::Import},
            {"var", TokenKind::Var},
            {"in", TokenKind::In},
            {"defer", TokenKind::Defer},
            {"panic", TokenKind::Panic},
        }
    };

    [[nodiscard]] constexpr auto lookup_keyword(std::string_view ident) -> std::optional<TokenKind> {
        for (const auto &kw: KEYWORDS) {
            if (kw.name == ident) return kw.kind;
        }
        return std::nullopt;
    }

    consteval bool keywords_are_valid() {
        for (std::size_t i = 0; i < KEYWORDS.size(); ++i) {
            for (std::size_t j = i + 1; j < KEYWORDS.size(); ++j) {
                if (KEYWORDS[i].name == KEYWORDS[j].name) return false;
                if (KEYWORDS[i].kind == KEYWORDS[j].kind) return false;
            }
        }
        return true;
    }

    static_assert(keywords_are_valid(), "keyword table has duplicate names or kinds");
}
