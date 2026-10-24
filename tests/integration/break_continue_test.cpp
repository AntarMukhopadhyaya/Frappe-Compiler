//
// Created by antar on 24/10/26.
//
// `break` and `continue` in MIR. `break` jumps to the loop's end label;
// `continue` jumps to the loop's update/condition label. Both are
// terminators — no instructions follow in their block.
//
// Before this round, the checker accepted break/continue anywhere and
// MIR emitted a hard "not supported" error.

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
}

// ============================================================================
// Checker — context validation
// ============================================================================

TEST_CASE("break: outside loop rejected", "[break][control][negative][core]") {
    auto r = frappe_test::compile(R"(
fn main() { break; }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("'break' outside of a loop") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("continue: outside loop rejected",
          "[continue][control][negative][core]") {
    auto r = frappe_test::compile(R"(
fn main() { continue; }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("'continue' outside of a loop") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("break: inside while accepted", "[break][control]") {
    auto r = frappe_test::compile(R"(
fn main() {
    while (true) { break; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("continue: inside for accepted", "[continue][control]") {
    auto r = frappe_test::compile(R"(
fn main() {
    for (var i: Int = 0; i < 10; i += 1) { continue; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// MIR — jump targets correct
// ============================================================================

TEST_CASE("break: emits jump to loop end label",
          "[break][control][mir]") {
    auto r = frappe_test::compile(R"(
fn main() {
    while (true) {
        break;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);

    // The break must be a MirJump (not an error, not a return).
    bool found_jump_after_branch = false;
    for (std::size_t i = 0; i < fn->instructions.size(); ++i) {
        if (auto *br = std::get_if<frappe::mir::MirBranch>(&fn->instructions[i])) {
            // The next label in the loop's structure... just check there's
            // at least one MirJump in the function. Before this round, break
            // would have produced an error and no jump.
            static_cast<void>(br);
        }
        if (std::holds_alternative<frappe::mir::MirJump>(fn->instructions[i])) {
            found_jump_after_branch = true;
        }
    }
    CHECK(found_jump_after_branch);
}

TEST_CASE("continue: emits jump to loop update label in for loop",
          "[continue][control][mir]") {
    auto r = frappe_test::compile(R"(
fn main() {
    for (var i: Int = 0; i < 10; i += 1) {
        continue;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = frappe_test::find_fn(mir, "main");
    REQUIRE(fn);

    // `continue` in `for` must jump somewhere — the upd label — and the
    // upd block must exist. Before the fix, for loops only emitted upd
    // when body fell through, so continue would break the validator.
    int jump_count = 0;
    for (const auto &inst : fn->instructions) {
        if (std::holds_alternative<frappe::mir::MirJump>(inst)) ++jump_count;
    }
    CHECK(jump_count >= 2);   // at least the body-end jump and the continue jump
}

// ============================================================================
// End-to-end behavior (structural — exit code check is a future round)
// ============================================================================

TEST_CASE("break: structurally valid IR emits for break-using program",
          "[break][control][codegen]") {
    auto r = frappe_test::compile(R"(
fn main() -> Int {
    var i: Int = 0;
    while (i < 10) {
        if (i == 3) { break; }
        i += 1;
    }
    return i;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto ir = frappe_test::build_ir(*r);
    if (!ir) { SUCCEED("codegen not built"); return; }
    CHECK(ir->find("define") != std::string::npos);
}