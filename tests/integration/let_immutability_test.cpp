//
// Created by antar on 24/10/26.
//
// `let` is an immutable binding. Reassignment (including compound
// assignment) must error. `var` bindings are mutable. Parameters are
// immutable (Kotlin/Rust convention — reassign to a local instead).
//
// Before this round, `let x = 0; x = 1;` compiled. Silent contract
// violation.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

TEST_CASE("let: reassignment rejected", "[let][immutability][core]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x: Int = 0;
    x = 1;
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("immutable binding 'x'") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("let: compound assignment rejected", "[let][immutability]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x: Int = 0;
    x += 1;
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("immutable binding 'x'") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("var: reassignment OK", "[let][immutability]") {
    auto r = frappe_test::compile(R"(
fn main() {
    var x: Int = 0;
    x = 1;
    x += 2;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("param: reassignment rejected", "[let][immutability]") {
    auto r = frappe_test::compile(R"(
fn f(x: Int) { x = 1; }
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("parameter 'x'") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("let: field assignment through reference is allowed",
          "[let][immutability]") {
    // `let p = new Point()` makes `p` itself immutable (can't reassign),
    // but `p.x = 5` is legal — the *reference* is immutable, not the
    // pointed-to object. Matches Kotlin `val`, Swift `let`, etc.
    auto r = frappe_test::compile(R"(
class Point { public x: Int; }
fn main() {
    let p = new Point();
    p.x = 5;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("let: array element assignment through reference is allowed",
          "[let][immutability]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int] = [1, 2, 3];
    xs[0] = 10;
}
)");
    CHECK_NO_ERRORS(r.get());
}