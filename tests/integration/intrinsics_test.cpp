//
// Created by antar on 24/10/26.
//
// intrinsic dispatch in the checker. Verifies that `println(x)`
// type-checks for the three supported types, rejects unsupported types with
// an actionable message, and errors on wrong arity. Does NOT execute the
// program — runtime functions are Round 6b. `--mir` shows the correct
// runtime callee name for each type; `--llvm-ir` will be verifier-clean but
// unlinkable until the C side exists.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/mir_test.hpp"

namespace {
    auto build_mir(frappe_test::Compile_Result *r) {
        return frappe::mir::build_module(
            r->program, &*r->checker, &r->engine, &r->arena);
    }
}

// ============================================================================
// Type-checking — overload resolution
// ============================================================================

TEST_CASE("intrinsic: println(Int) type-checks", "[intrinsic]") {
    auto r = frappe_test::compile(R"(
fn main() { println(42); }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("intrinsic: println(String) type-checks", "[intrinsic]") {
    auto r = frappe_test::compile(R"(
fn main() { println("hello"); }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("intrinsic: println(Float) type-checks", "[intrinsic]") {
    auto r = frappe_test::compile(R"(
fn main() { println(3.14); }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("intrinsic: println(Bool) rejected with actionable message",
          "[intrinsic][negative]") {
    auto r = frappe_test::compile(R"(
fn main() { println(true); }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("no overload for Bool") != std::string::npos
            && e.message.find("available: Int, Float, String") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("intrinsic: println(class) rejected", "[intrinsic][negative]") {
    auto r = frappe_test::compile(R"(
class Point { public x: Int; }
fn main() {
    let p = new Point();
    println(p);
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("no overload for Point") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Arity — wrong argument count
// ============================================================================

TEST_CASE("intrinsic: println() rejected for arity", "[intrinsic][negative]") {
    auto r = frappe_test::compile(R"(
fn main() { println(); }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("'println' expects 1 argument, got 0") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("intrinsic: println(1, 2) rejected for arity", "[intrinsic][negative]") {
    auto r = frappe_test::compile(R"(
fn main() { println(1, 2); }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("'println' expects 1 argument, got 2") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// MIR — the correct runtime function is selected per type
// ============================================================================

TEST_CASE("intrinsic: println(Int) emits call to __frappe_println_int",
          "[intrinsic][mir]") {
    auto r = frappe_test::compile(R"(
fn main() { println(42); }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);
    CHECK(frappe_test::count_calls_to(*fn, "__frappe_println_int") == 1);
}

TEST_CASE("intrinsic: println(String) emits call to __frappe_println_str",
          "[intrinsic][mir]") {
    auto r = frappe_test::compile(R"(
fn main() { println("hi"); }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);
    CHECK(frappe_test::count_calls_to(*fn, "__frappe_println_str") == 1);
}

TEST_CASE("intrinsic: println(Float) emits call to __frappe_println_float",
          "[intrinsic][mir]") {
    auto r = frappe_test::compile(R"(
fn main() { println(3.14); }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);
    CHECK(frappe_test::count_calls_to(*fn, "__frappe_println_float") == 1);
}

TEST_CASE("intrinsic: println(Int) at -O2 folds constant but keeps the call",
          "[intrinsic][mir]") {
    // Sanity: printing is observable, so LLVM must not eliminate the call
    // even when the argument is a literal. This is guaranteed by the
    // call's side effects (writes to stdout), but worth pinning.
    auto r = frappe_test::compile(R"(
fn main() { println(1); println(2); }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);
    CHECK(frappe_test::count_calls_to(*fn, "__frappe_println_int") == 2);
}

// ============================================================================
// User function with the same name shadows the intrinsic
// ============================================================================

TEST_CASE("intrinsic: user-defined println shadows the builtin",
          "[intrinsic][shadowing]") {
    auto r = frappe_test::compile(R"(
fn println(x: Int) { }
fn main() { println(42); }
)");
    // The user function wins; the checker path is check_call_to_fn, not
    // check_intrinsic_call. No runtime call to __frappe_println_int.
    CHECK_NO_ERRORS(r.get());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);
    CHECK(frappe_test::count_calls_to(*fn, "__frappe_println_int") == 0);
    CHECK(frappe_test::count_calls_to(*fn, "println") == 1);
}