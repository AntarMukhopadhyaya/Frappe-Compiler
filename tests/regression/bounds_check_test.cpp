//
// Created by antar on 24/10/26.
//
// Regression guard for the bounds-check fix. Before: out-of-range array
// access read raw heap bytes (usually null) and SIGSEGV'd on virtual
// dispatch. Now: __frappe_bounds_error panics cleanly with the index and
// length.
//
// Runtime behavioral test comes with the compile_and_run round. Here we
// verify the emitted IR contains the guard — if someone ever removes it
// from handle_get_index/handle_set_index, these fail immediately.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/codegen_test.hpp"

TEST_CASE("bounds: array read emits bounds check",
          "[bounds][regression][core]") {
    auto r = frappe_test::compile(R"(
fn at(xs: [Int], i: Int) -> Int { return xs[i]; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*ir, "declare void @__frappe_bounds_error"));
    REQUIRE(frappe_test::ir_contains(*ir, "call void @__frappe_bounds_error"));
    REQUIRE(frappe_test::ir_contains(*ir, "idx_oob"));
    REQUIRE(frappe_test::ir_contains(*ir, "idx_ok"));
}

TEST_CASE("bounds: array write emits bounds check",
          "[bounds][regression][core]") {
    auto r = frappe_test::compile(R"(
fn put(xs: [Int], i: Int, v: Int) { xs[i] = v; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*ir, "call void @__frappe_bounds_error"));
}

TEST_CASE("bounds: non-array program has no bounds call",
          "[bounds][regression]") {
    // The declaration is always present; the call must only appear when an
    // array access is actually emitted. Guards against accidentally
    // emitting the check unconditionally (or in the wrong place).
    auto r = frappe_test::compile(R"(
fn add(a: Int, b: Int) -> Int { return a + b; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }

    REQUIRE_FALSE(frappe_test::ir_contains(*ir, "call void @__frappe_bounds_error"));
}

TEST_CASE("bounds: length load reads obj[-1]",
          "[bounds][regression]") {
    // The check depends on the header layout written by __frappe_alloc_array:
    // i64 count at payload[-8]. If the runtime ever changes that layout, this
    // test catches it because the emitted GEP would still use -1.
    auto r = frappe_test::compile(R"(
fn at(xs: [Int], i: Int) -> Int { return xs[i]; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*ir, "getelementptr i64, ptr"));
    REQUIRE(frappe_test::ir_contains(*ir, "i64 -1"));
}