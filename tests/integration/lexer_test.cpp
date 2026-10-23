//
// Created by antar on 21/10/26.
//


#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

using frappe::syntax::TokenKind;

namespace {
void check_kind(const frappe_test::Lex_Result* r, std::size_t idx, TokenKind k) {
    REQUIRE(idx < r->tokens.size());
    INFO("token[" << idx << "].kind");
    REQUIRE(r->tokens[idx].kind == k);
}

void check_lexeme(const frappe_test::Lex_Result* r, std::size_t idx,
                  std::string_view lex) {
    REQUIRE(idx < r->tokens.size());
    INFO("token[" << idx << "].lexeme = '"
         << std::string{r->tokens[idx].lexeme} << "'");
    REQUIRE(r->tokens[idx].lexeme == lex);
}
} // namespace

TEST_CASE("empty source yields only EOF", "[lexer]") {
    auto r = frappe_test::lex("");
    REQUIRE_FALSE(r->has_errors());
    REQUIRE(r->tokens.size() == 1);
    check_kind(r.get(), 0, TokenKind::Eof);
}

TEST_CASE("keywords and identifiers tokenize correctly", "[lexer]") {
    auto r = frappe_test::lex("fn main class Foo");
    REQUIRE_FALSE(r->has_errors());
    REQUIRE(r->tokens.size() == 5);
    check_kind  (r.get(), 0, TokenKind::Fn);     check_lexeme(r.get(), 0, "fn");
    check_kind  (r.get(), 1, TokenKind::Ident);  check_lexeme(r.get(), 1, "main");
    check_kind  (r.get(), 2, TokenKind::Class);  check_lexeme(r.get(), 2, "class");
    check_kind  (r.get(), 3, TokenKind::Ident);  check_lexeme(r.get(), 3, "Foo");
    check_kind  (r.get(), 4, TokenKind::Eof);
}

TEST_CASE("line comments are skipped", "[lexer]") {
    auto r = frappe_test::lex("let x // this is a comment\nlet y");
    REQUIRE_FALSE(r->has_errors());
    // let x let y EOF
    REQUIRE(r->tokens.size() == 5);
    check_kind(r.get(), 2, TokenKind::Let);
    check_kind(r.get(), 3, TokenKind::Ident);
}

TEST_CASE("operators tokenize with correct kinds", "[lexer]") {
    auto r = frappe_test::lex("+ - * / % == != <= >= && || ! = += -= ");
    REQUIRE_FALSE(r->has_errors());

    std::vector<TokenKind> expected = {
        TokenKind::Plus,  TokenKind::Minus,  TokenKind::Star,
        TokenKind::Slash, TokenKind::Mod,    TokenKind::EqEq,
        TokenKind::Neq,   TokenKind::Lte,    TokenKind::Gte,
        TokenKind::And,   TokenKind::Or,     TokenKind::Not,
        TokenKind::Eq,    TokenKind::PlusEq, TokenKind::MinusEq,
        TokenKind::Eof,
    };
    REQUIRE(r->tokens.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check_kind(r.get(), i, expected[i]);
    }
}

TEST_CASE("int and float literals are distinguished", "[lexer]") {
    auto r = frappe_test::lex("42 3.14 2.0");
    REQUIRE_FALSE(r->has_errors());
    REQUIRE(r->tokens.size() == 4);
    check_kind(r.get(), 0, TokenKind::Int);
    check_kind(r.get(), 1, TokenKind::Float);
    check_kind(r.get(), 2, TokenKind::Float);
}

TEST_CASE("string literals preserve content", "[lexer]") {
    auto r = frappe_test::lex("\"hello world\"");
    REQUIRE_FALSE(r->has_errors());
    REQUIRE(r->tokens.size() == 2);
    check_kind  (r.get(), 0, TokenKind::Str);
    check_lexeme(r.get(), 0, "hello world");
}