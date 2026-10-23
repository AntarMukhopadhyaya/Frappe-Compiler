//
// Created by antar on 21/10/26.
//
#include <catch2/catch_test_macros.hpp>

#include <frappe/syntax/keywords.hpp>

using namespace frappe::syntax;

TEST_CASE("keyword lookup resolves every keyword", "[syntax]") {
    REQUIRE(lookup_keyword("fn").value()         == TokenKind::Fn);
    REQUIRE(lookup_keyword("class").value()      == TokenKind::Class);
    REQUIRE(lookup_keyword("enum").value()       == TokenKind::Enum);
    REQUIRE(lookup_keyword("interface").value()  == TokenKind::Interface);
    REQUIRE(lookup_keyword("implements").value() == TokenKind::Implements);
    REQUIRE(lookup_keyword("extends").value()    == TokenKind::Extends);
    REQUIRE(lookup_keyword("new").value()        == TokenKind::New);
    REQUIRE(lookup_keyword("this").value()       == TokenKind::This);
    REQUIRE(lookup_keyword("super").value()      == TokenKind::Super);
    REQUIRE(lookup_keyword("let").value()        == TokenKind::Let);
    REQUIRE(lookup_keyword("var").value()        == TokenKind::Var);
    REQUIRE(lookup_keyword("return").value()     == TokenKind::Return);
    REQUIRE(lookup_keyword("if").value()         == TokenKind::If);
    REQUIRE(lookup_keyword("else").value()       == TokenKind::Else);
    REQUIRE(lookup_keyword("for").value()        == TokenKind::For);
    REQUIRE(lookup_keyword("while").value()      == TokenKind::While);
}

TEST_CASE("keyword lookup rejects non-keywords", "[syntax]") {
    REQUIRE_FALSE(lookup_keyword("").has_value());
    REQUIRE_FALSE(lookup_keyword("banana").has_value());
    REQUIRE_FALSE(lookup_keyword("Init").has_value());   // case-sensitive
    REQUIRE_FALSE(lookup_keyword("init").has_value());   // not a keyword
    REQUIRE_FALSE(lookup_keyword("deinit").has_value()); // not a keyword
    REQUIRE_FALSE(lookup_keyword("init ").has_value());  // trailing space
}

TEST_CASE("keyword table has no duplicates", "[syntax]") {
    for (std::size_t i = 0; i < KEYWORDS.size(); ++i) {
        for (std::size_t j = i + 1; j < KEYWORDS.size(); ++j) {
            REQUIRE(KEYWORDS[i].name != KEYWORDS[j].name);
            REQUIRE(KEYWORDS[i].kind != KEYWORDS[j].kind);
        }
    }
}