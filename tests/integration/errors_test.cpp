//
// Created by antar on 21/10/26.
//
#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

TEST_CASE("empty source compiles cleanly", "[checker]") {
    auto r = frappe_test::compile("");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("trivial function compiles cleanly", "[checker]") {
    auto r = frappe_test::compile(R"(
fn main() {}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("type mismatch on variable init", "[checker]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x: Int = "hello";
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "type mismatch");
    CHECK_ERROR_LINE(r.get(), 0, 3, "expected Int");
    CHECK_ERROR_LINE(r.get(), 0, 3, "got String");
}

TEST_CASE("function arity mismatch", "[checker]") {
    auto r = frappe_test::compile(R"(
fn add(a: Int, b: Int) -> Int { return a + b; }
fn main() { let x = add(1); }
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "expects 2 arguments, got 1");
}

TEST_CASE("arithmetic on non-numeric", "[checker]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x = 5 + "hi";
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "addition requires numeric operands");
}

TEST_CASE("comparison on non-numeric", "[checker]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x = 5 > "hi";
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "comparison requires numeric operands");
}

TEST_CASE("logical operator requires Bool", "[checker]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x = 5 && true;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "logical operator requires Bool operands");
}

TEST_CASE("unary '!' requires Bool", "[checker]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x = !42;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "unary '!' requires Bool operand");
}

TEST_CASE("assignment to non-lvalue", "[checker]") {
    auto r = frappe_test::compile(R"(
fn f() -> Int { return 0; }
fn main() {
    f() = 5;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 4, "left side of assignment");
}

TEST_CASE("unknown identifier", "[resolver]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x = nonexistent;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "unknown identifier 'nonexistent'");
}

TEST_CASE("unknown type", "[resolver]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x: UnknownType = 1;
}
)");
    REQUIRE(r->error_count() >= 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "unknown type 'UnknownType'");
}

TEST_CASE("duplicate declaration in same scope", "[resolver]") {
    auto r = frappe_test::compile(R"(
fn dup() {}
fn dup() {}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "already declared");
}