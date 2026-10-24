//
// Created by antar on 24/10/26.
//
// Regression guard for the scope-stack refactor. Verifies that
// per-instance caches survive after the body check completes, so MIR
// builder / codegen can read them during the emit phase.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

TEST_CASE("scope stack: instances have isolated scopes after check",
          "[checker][scope][refactor][core]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() {
    let a = identity(1);
    let b = identity("hi");
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->checker.has_value());
    REQUIRE(r->checker->instances.size() == 2);

    // Each instance's scope must be populated (param types, body node types).
    for (const auto &[name, inst] : r->checker->instances) {
        INFO("instance: " << name);
        CHECK_FALSE(inst->scope.node_types.empty());
        CHECK_FALSE(inst->scope.symbol_types.empty());
    }

    // The two instances must have distinct node-type maps — if they share
    // storage, that's the bug we're trying to prevent.
    auto it = r->checker->instances.begin();
    auto *first = it->second;
    ++it;
    auto *second = it->second;
    CHECK(first->scope.node_types.size() > 0);
    CHECK(second->scope.node_types.size() > 0);
    // Sanity: they're distinct objects (can't compare contents directly
    // since keys are AST node pointers that differ per instantiation).
    CHECK(first != second);
}

TEST_CASE("scope stack: empty after check_program completes",
          "[checker][scope][refactor]") {
    // Context_Guard pops what it pushes. If any path leaves a dangling
    // scope, subsequent checks would route writes to a stale map.
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() { let a = identity(1); }
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->checker.has_value());
    CHECK(r->checker->scope_stack.empty());
}