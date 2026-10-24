//
// Created by antar on 24/10/26.
//
// Regression guard for short-circuit evaluation. Before this fix,
// lower_binary lowered both operands before the branch, so `a && f()`
// always called f() even when a was false — silent wrong behavior,
// invisible to the codegen tests because they only check for presence
// of branch IR, not the ordering of rhs's instructions relative to
// the branch.
//
// These tests verify ordering at the MIR level: the rhs call must
// appear AFTER the first MirBranch in the same function.
//
// A behavioral test (running the program and observing that rhs was
// never called) requires compile_and_run and lands in the next round.

#include <catch2/catch_test_macros.hpp>

#include <frappe/mir/builder.hpp>
#include <frappe/mir/instruction.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/mir_test.hpp"

namespace {
    auto build_mir(frappe_test::Compile_Result *r) {
        return frappe::mir::build_module(
            r->program, &*r->checker, &r->engine, &r->arena);
    }

    std::size_t first_branch_idx(const frappe::mir::MirFunction &fn) {
        for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
            if (std::holds_alternative<frappe::mir::MirBranch>(fn.instructions[i])) {
                return i;
            }
        }
        return static_cast<std::size_t>(-1);
    }
}

// ============================================================================
// Core — rhs emitted AFTER the branch
// ============================================================================

TEST_CASE("short-circuit: && rhs call appears after the branch",
          "[short-circuit][mir][core]") {
    auto r = frappe_test::compile(R"(
fn side_effect() -> Bool { return true; }
fn check(a: Bool) -> Bool {
    return a && side_effect();
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "check");
    REQUIRE(fn);

    auto branch_idx = first_branch_idx(*fn);
    auto call_idx = frappe_test::first_call_index(*fn, "side_effect");

    REQUIRE(branch_idx != static_cast<std::size_t>(-1));
    REQUIRE(call_idx != static_cast<std::size_t>(-1));
    CHECK(call_idx > branch_idx);
}

TEST_CASE("short-circuit: || rhs call appears after the branch",
          "[short-circuit][mir][core]") {
    auto r = frappe_test::compile(R"(
fn side_effect() -> Bool { return true; }
fn check(a: Bool) -> Bool {
    return a || side_effect();
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "check");
    REQUIRE(fn);

    auto branch_idx = first_branch_idx(*fn);
    auto call_idx = frappe_test::first_call_index(*fn, "side_effect");

    REQUIRE(branch_idx != static_cast<std::size_t>(-1));
    REQUIRE(call_idx != static_cast<std::size_t>(-1));
    CHECK(call_idx > branch_idx);
}

// ============================================================================
// Nested — both inner rhs calls come after the outer branch
// ============================================================================

TEST_CASE("short-circuit: nested && / || both defer to after first branch",
          "[short-circuit][mir]") {
    auto r = frappe_test::compile(R"(
fn a_fn() -> Bool { return true; }
fn b_fn() -> Bool { return true; }
fn check(x: Bool, y: Bool) -> Bool {
    return x && (y && a_fn()) || b_fn();
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "check");
    REQUIRE(fn);

    auto branch_idx = first_branch_idx(*fn);
    auto a_idx = frappe_test::first_call_index(*fn, "a_fn");
    auto b_idx = frappe_test::first_call_index(*fn, "b_fn");

    REQUIRE(branch_idx != static_cast<std::size_t>(-1));
    REQUIRE(a_idx != static_cast<std::size_t>(-1));
    REQUIRE(b_idx != static_cast<std::size_t>(-1));
    CHECK(a_idx > branch_idx);
    CHECK(b_idx > branch_idx);
}

// ============================================================================
// Structural sanity
// ============================================================================

TEST_CASE("short-circuit: && with constant rhs still branches",
          "[short-circuit][mir]") {
    auto r = frappe_test::compile(R"(
fn check(a: Bool) -> Bool {
    return a && true;
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "check");
    REQUIRE(fn);
    REQUIRE(frappe_test::count_branches(*fn) >= 1);
    REQUIRE(frappe_test::is_well_formed(*fn));
}

TEST_CASE("short-circuit: && control flow is well-formed",
          "[short-circuit][mir]") {
    auto r = frappe_test::compile(R"(
fn check(a: Bool, b: Bool) -> Bool {
    return a && b;
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "check");
    REQUIRE(fn);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}