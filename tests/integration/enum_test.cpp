//
// Created by antar on 24/10/26.
//
// Round: integer enums. Verification spans:
//   - Checker: declaration, variant access, type safety
//   - MIR: variant access lowers to constant
//   - Codegen: enum type maps to i64
//   - Runtime: --run demo prints correctly (manual, not in this file)

#include <catch2/catch_test_macros.hpp>

#include <frappe/mir/builder.hpp>
#include <frappe/mir/instruction.hpp>
#include <frappe/types/types.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/codegen_test.hpp"
#include "../helpers/mir_test.hpp"

namespace {
    auto build_mir(frappe_test::Compile_Result *r) {
        return frappe::mir::build_module(
            r->program, &*r->checker, &r->engine, &r->arena);
    }
}

// ============================================================================
// Declaration
// ============================================================================

TEST_CASE("enum: basic declaration compiles", "[enum]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("enum: explicit values accepted", "[enum]") {
    auto r = frappe_test::compile(R"(
enum Code { Ok = 0, Created = 201, Error = 500 }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("enum: mixed explicit and auto values accepted", "[enum]") {
    auto r = frappe_test::compile(R"(
enum Code { A, B = 10, C, D = 100, E }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("enum: duplicate variant rejected",
          "[enum][negative][core]") {
    auto r = frappe_test::compile(R"(
enum Bad { A, B, A }
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("duplicate enum variant 'A'") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("enum: non-integer variant value rejected",
          "[enum][negative][core]") {
    auto r = frappe_test::compile(R"(
enum Bad { A = "red" }
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("must be an integer literal") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Variant access
// ============================================================================

TEST_CASE("enum: variant access type-checks as enum type",
          "[enum][checker][core]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() {
    let c: Color = Color.Red;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("enum: unknown variant rejected", "[enum][negative]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() {
    let c: Color = Color.Purple;
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("enum 'Color' has no variant 'Purple'") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("enum: integer not assignable to enum type",
          "[enum][type-safety][core]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() {
    let c: Color = 0;
}
)");
    REQUIRE(r->has_errors());
}

TEST_CASE("enum: enum not assignable to Int",
          "[enum][type-safety]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() {
    let x: Int = Color.Red;
}
)");
    REQUIRE(r->has_errors());
}

TEST_CASE("enum: different enums not comparable",
          "[enum][type-safety]") {
    auto r = frappe_test::compile(R"(
enum Color { Red }
enum Direction { North }
fn main() {
    if (Color.Red == Direction.North) { }
}
)");
    REQUIRE(r->has_errors());
}

TEST_CASE("enum: same enum values comparable",
          "[enum][comparison]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() {
    if (Color.Red == Color.Red) { }
    if (Color.Red != Color.Blue) { }
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// MIR — variant access lowers to constant
// ============================================================================

TEST_CASE("enum: variant lowers to MirConstant",
          "[enum][mir][core]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn main() {
    let a = Color.Red;
    let b = Color.Green;
    let c = Color.Blue;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);

    // Three constants expected: 0, 1, 2.
    REQUIRE(fn->constants.size() >= 3);
    std::size_t found_0 = 0, found_1 = 0, found_2 = 0;
    for (const auto &c : fn->constants) {
        if (auto *v = std::get_if<std::int64_t>(&c)) {
            if (*v == 0) ++found_0;
            if (*v == 1) ++found_1;
            if (*v == 2) ++found_2;
        }
    }
    CHECK(found_0 >= 1);
    CHECK(found_1 >= 1);
    CHECK(found_2 >= 1);
}

TEST_CASE("enum: explicit values lower correctly",
          "[enum][mir]") {
    auto r = frappe_test::compile(R"(
enum Code { A = 10, B = 20 }
fn main() {
    let a = Code.A;
    let b = Code.B;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);

    bool found_10 = false, found_20 = false;
    for (const auto &c : fn->constants) {
        if (auto *v = std::get_if<std::int64_t>(&c)) {
            if (*v == 10) found_10 = true;
            if (*v == 20) found_20 = true;
        }
    }
    CHECK(found_10);
    CHECK(found_20);
}

TEST_CASE("enum: mixed values use prev+1 for auto-assigned",
          "[enum][mir]") {
    auto r = frappe_test::compile(R"(
enum Code { A, B = 10, C, D = 100, E }
fn main() {
    let a = Code.A;   // 0
    let b = Code.B;   // 10
    let c = Code.C;   // 11
    let d = Code.D;   // 100
    let e = Code.E;   // 101
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);

    auto has_const = [&](std::int64_t v) {
        for (const auto &c : fn->constants) {
            if (auto *p = std::get_if<std::int64_t>(&c); p && *p == v) return true;
        }
        return false;
    };
    CHECK(has_const(0));
    CHECK(has_const(10));
    CHECK(has_const(11));
    CHECK(has_const(100));
    CHECK(has_const(101));
}

// ============================================================================
// Switch on enum
// ============================================================================

TEST_CASE("enum: switch on enum type-checks", "[enum][switch]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn describe(c: Color) -> Int {
    switch (c) {
        case Color.Red: return 1;
        case Color.Green: return 2;
        case Color.Blue: return 3;
    }
    return 0;
}
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Codegen — enum type is i64
// ============================================================================

TEST_CASE("enum: enum param type emits as i64",
          "[enum][codegen]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn first(c: Color) -> Int { return 1; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }

    // Function signature: enum param → i64, not ptr.
    REQUIRE(frappe_test::ir_contains(*ir, "define i64 @first(i64"));
}

TEST_CASE("enum: comparison emits icmp on i64",
          "[enum][codegen]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green }
fn check(c: Color) -> Bool { return c == Color.Red; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*ir, "icmp eq i64"));
}

TEST_CASE("enum: inferred local var has i64 codegen type",
          "[enum][codegen][core]") {
    auto r = frappe_test::compile(R"(
enum Color { Red, Green, Blue }
fn first(c: Color) -> Int { return 0; }
fn main() {
    let c = Color.Red;
    let x = first(c);
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) {
        for (const auto &e : r->errors) {
            UNSCOPED_INFO("[codegen error] " << e.message);
        }
        FAIL("codegen failed to produce verifier-clean IR");
        return;
    }
    CHECK(frappe_test::ir_contains(*ir, "alloca i64"));
    CHECK(frappe_test::ir_contains(*ir, "call i64 @first(i64"));
}

TEST_CASE("inferred int local has correct codegen type",
          "[codegen][regression]") {
    auto r = frappe_test::compile(R"(
fn use(x: Int) -> Int { return x; }
fn main() {
    let y = 5;
    let z = use(y);
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) {
        for (const auto &e : r->errors) {
            UNSCOPED_INFO("[codegen error] " << e.message);
        }
        FAIL("codegen failed to produce verifier-clean IR");
        return;
    }
    CHECK(frappe_test::ir_contains(*ir, "alloca i64"));
}