//
// Created by antar on 19/10/26.
//

#pragma  once
#include <cstdint>

namespace frappe::syntax {
    enum class TokenKind : std::uint8_t {
        Eof,
        Ident,
        // Keywords
        Fn, Class, Enum, Break, Continue, Return, If, Else, For, While,
        Switch, Case, Default, False, True, Nil, This, Final, Super,
        Private, Public, Protected, Static, Override, Extends, Implements,
        Interface, Abstract, New, Let, As, Import, Var, In, Defer, Panic,

        // Literals
        Str, Char, Int, Float,

        // Delimiters
        LParen, RParen, LBrace, RBrace, LBrack, RBrack,
        Comma, Question, Colon, Dot, Arrow, SemiColon,

        // Arithmetic
        Plus, Minus, Slash, Star, Mod,

        // Compound assignment
        PlusEq, MinusEq, SlashEq, ModEq, StarEq,

        // Comparison / logical
        Gt, Gte, Lt, Lte, Or, And, Not,

        // Equality / assignment
        EqEq, Eq, Neq,
    };
}
