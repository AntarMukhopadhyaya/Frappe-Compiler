//
// Created by antar on 19/10/26.
//

#pragma  once
#include <string_view>
#include <frappe/source/span.hpp>
#include <frappe/syntax/token_kind.hpp>


namespace  frappe::syntax {
    struct Token {
        TokenKind kind;
        std::string_view lexeme;
        source::Span span;
    };
}