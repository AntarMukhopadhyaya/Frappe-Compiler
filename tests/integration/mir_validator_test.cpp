//
// Created by antar on 23/10/26.
//

#include <catch2/catch_test_macros.hpp>

#include <frappe/mir/builder.hpp>
#include <frappe/mir/validate.hpp>

#include "../helpers/compile.hpp"

namespace {

    auto build_mir(frappe_test::Compile_Result *r) {
        return frappe::mir::build_module(
            r->program, &*r->checker, &r->engine, &r->arena);
    }

} // namespace

// ============================================================================
// Sanity: valid programs produce no validation errors
// ============================================================================

TEST_CASE("validator: empty main produces no errors", "[mir][validator]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    CHECK(frappe::mir::validate_module(mir, &r->arena).empty());
}

TEST_CASE("validator: arithmetic produces no errors", "[mir][validator]") {
    auto r = frappe_test::compile(R"(
fn add(a: Int, b: Int) -> Int { return a + b; }
fn main() { let _x = add(1, 2); }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    CHECK(frappe::mir::validate_module(mir, &r->arena).empty());
}

TEST_CASE("validator: class with methods produces no errors",
          "[mir][validator]") {
    auto r = frappe_test::compile(R"(
class P {
    public x: Int;
    fn init() { this.x = 1; }
    fn get() -> Int { return this.x; }
}
fn main() { let p = new P(); }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    CHECK(frappe::mir::validate_module(mir, &r->arena).empty());
}

TEST_CASE("validator: build_module does not spuriously report on valid MIR",
          "[mir][validator][integration]") {
    // The validator runs inside build_module. Valid programs must not
    // trigger it.
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    static_cast<void>(build_mir(r.get()));
    CHECK_FALSE(r->has_errors());
}

// ============================================================================
// Category A — ID bounds
// ============================================================================

TEST_CASE("validator: out-of-range temp flagged", "[mir][validator][bounds]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    // Function 0 is __frappe_user_main (main is the only user function here).
    auto &fn = mir.functions[0];
    fn.instructions.push_back(frappe::mir::MirMove{
        frappe::mir::MirTemp{.index = 9999},
        frappe::mir::MirConstantId{.index = 0},
    });

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found = false;
    for (const auto &e: errors) {
        if (e.message.find("9999") != std::string::npos) { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("validator: out-of-range local in parameters flagged",
          "[mir][validator][bounds]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    auto &fn = mir.functions[0];
    fn.parameters.push_back(frappe::mir::MirLocalId{.index = 9999});

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found = false;
    for (const auto &e: errors) {
        if (e.message.find("9999") != std::string::npos
            && e.message.find("local") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("validator: out-of-range constant flagged",
          "[mir][validator][bounds]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    auto &fn = mir.functions[0];
    fn.instructions.push_back(frappe::mir::MirMove{
        frappe::mir::MirTemp{.index = 0},
        frappe::mir::MirConstantId{.index = 9999},
    });
    // Ensure temp 0 is valid so this test isolates the constant check.
    // main() has no temps but does have returns... actually let's not
    // rely on that — check for both errors is fine.
    if (fn.temp_types.empty()) {
        fn.temp_types.push_back(r->checker->store.int_type);
    }

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found = false;
    for (const auto &e: errors) {
        if (e.message.find("9999") != std::string::npos
            && e.message.find("constant") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Category B — label references
// ============================================================================

TEST_CASE("validator: jump to undefined label flagged",
          "[mir][validator][labels]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    auto &fn = mir.functions[0];
    fn.instructions.push_back(
        frappe::mir::MirJump{frappe::mir::MirLabel{.index = 9999}});

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found = false;
    for (const auto &e: errors) {
        if (e.message.find("L9999") != std::string::npos) { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("validator: branch to undefined label flagged",
          "[mir][validator][labels]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    auto &fn = mir.functions[0];
    fn.instructions.push_back(frappe::mir::MirBranch{
        frappe::mir::MirConstantId{.index = 0},
        frappe::mir::MirLabel{.index = 9999},
        frappe::mir::MirLabel{.index = 8888},
    });
    if (fn.constants.empty()) {
        fn.constants.push_back(frappe::mir::MirConstant{std::int64_t{0}});
    }

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found_9999 = false, found_8888 = false;
    for (const auto &e: errors) {
        if (e.message.find("L9999") != std::string::npos) found_9999 = true;
        if (e.message.find("L8888") != std::string::npos) found_8888 = true;
    }
    CHECK(found_9999);
    CHECK(found_8888);
}

// ============================================================================
// Category C — terminator placement
// ============================================================================

TEST_CASE("validator: missing terminator flagged",
          "[mir][validator][terminators]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    auto &fn = mir.functions[0];
    // Replace the last instruction (which is MirReturn) with a MirMove.
    if (fn.temp_types.empty()) {
        fn.temp_types.push_back(r->checker->store.int_type);
    }
    if (fn.constants.empty()) {
        fn.constants.push_back(frappe::mir::MirConstant{std::int64_t{0}});
    }
    fn.instructions.back() = frappe::mir::MirMove{
        frappe::mir::MirTemp{.index = 0},
        frappe::mir::MirConstantId{.index = 0},
    };

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found = false;
    for (const auto &e: errors) {
        if (e.message.find("terminator") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("validator: instruction after terminator flagged",
          "[mir][validator][terminators]") {
    auto r = frappe_test::compile("fn main() { }");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());

    auto &fn = mir.functions[0];
    if (fn.temp_types.empty()) {
        fn.temp_types.push_back(r->checker->store.int_type);
    }
    if (fn.constants.empty()) {
        fn.constants.push_back(frappe::mir::MirConstant{std::int64_t{0}});
    }
    // Append a non-label instruction after the trailing MirReturn.
    fn.instructions.push_back(frappe::mir::MirMove{
        frappe::mir::MirTemp{.index = 0},
        frappe::mir::MirConstantId{.index = 0},
    });

    auto errors = frappe::mir::validate_module(mir, &r->arena);
    REQUIRE_FALSE(errors.empty());
    bool found = false;
    for (const auto &e: errors) {
        if (e.message.find("follows a terminator") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}