//
// Created by antar on 22/10/26.
//


#include <catch2/catch_test_macros.hpp>
#include <frappe/mir/builder.hpp>
#include <variant>
#include "../helpers/compile.hpp"
#include "../helpers/mir_test.hpp"

namespace {
auto build_mir(frappe_test::Compile_Result* r) {
    return frappe::mir::build_module(
        r->program, &*r->checker, &r->engine, &r->arena);
}
} // namespace

// ============================================================================
// Callable
// ============================================================================

TEST_CASE("static method is callable on class name", "[static]") {
    auto r = frappe_test::compile(R"(
class Math {
    static fn add(a: Int, b: Int) -> Int { return a + b; }
}
fn main() {
    let x = Math.add(1, 2);
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("static method with no args", "[static]") {
    auto r = frappe_test::compile(R"(
class Const {
    static fn answer() -> Int { return 42; }
}
fn main() {
    let x = Const.answer();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("static method with wrong arg count errors", "[static]") {
    auto r = frappe_test::compile(R"(
class Math {
    static fn add(a: Int, b: Int) -> Int { return a + b; }
}
fn main() {
    let x = Math.add(1);
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 6, "expects 2 arguments");
}

TEST_CASE("static method with wrong arg type errors", "[static]") {
    auto r = frappe_test::compile(R"(
class Math {
    static fn add(a: Int, b: Int) -> Int { return a + b; }
}
fn main() {
    let x = Math.add(1, "two");
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 6, "type mismatch");
}

// ============================================================================
// Misuse
// ============================================================================

TEST_CASE("instance method not callable on class name", "[static]") {
    auto r = frappe_test::compile(R"(
class Point {
    fn get() -> Int { return 0; }
}
fn main() {
    let x = Point.get();
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 6, "is an instance method");
}

TEST_CASE("unknown static method errors", "[static]") {
    auto r = frappe_test::compile(R"(
class Math { }
fn main() {
    let x = Math.missing();
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 4, "has no static method 'missing'");
}

// ============================================================================
// Body restrictions
// ============================================================================

TEST_CASE("this not valid in static method", "[static]") {
    auto r = frappe_test::compile(R"(
class Foo {
    static fn bad() {
        let x = this;
    }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 4, "'this' is not valid in a static method");
}

TEST_CASE("static and abstract contradictory", "[static]") {
    auto r = frappe_test::compile(R"(
abstract class Foo {
    static abstract fn bar() -> Int;
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("both 'static' and 'abstract'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("static and override contradictory", "[static]") {
    auto r = frappe_test::compile(R"(
class Parent { }
class Child extends Parent {
    static override fn bar() { }
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("both 'static' and 'override'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("static init is rejected", "[static]") {
    auto r = frappe_test::compile(R"(
class Foo {
    static fn init() { }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'init' cannot be static");
}

// ============================================================================
// Inheritance — static methods inherited
// ============================================================================

TEST_CASE("inherited static method callable via subclass name", "[static]") {
    auto r = frappe_test::compile(R"(
class Base {
    static fn helper() -> Int { return 1; }
}
class Derived extends Base { }
fn main() {
    let x = Derived.helper();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("inherited static resolves to declaring class name", "[static]") {
    auto r = frappe_test::compile(R"(
class Base {
    static fn helper() -> Int { return 1; }
}
class Derived extends Base { }
fn main() {
    let x = Derived.helper();
}
)");
    REQUIRE_FALSE(r->has_errors());

    // The recorded callee name should be "Base.helper", not "Derived.helper".
    bool found = false;
    for (const auto& [node, name] : r->checker->call_names) {
        (void)node;
        if (name == "Base.helper") { found = true; break; }
    }
    REQUIRE(found);
}

// ============================================================================
// MIR — no receiver emitted for static call
// ============================================================================

TEST_CASE("static call emits no receiver in MIR", "[static][mir]") {
    auto r = frappe_test::compile(R"(
class Math {
    static fn add(a: Int, b: Int) -> Int { return a + b; }
}
fn main() {
    let x = Math.add(1, 2);
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* main_fn = frappe_test::find_fn(mod, "main");
    REQUIRE(main_fn);

    bool found = false;
    for (const auto& inst : main_fn->instructions) {
        if (auto* call = std::get_if<frappe::mir::MirCall>(&inst)) {
            if (auto* direct = std::get_if<frappe::mir::MirDirectCallee>(&call->callee)) {
                if (direct->name == "Math.add") {
                    // Two user args, NO receiver. If this is 3, the
                    // receiver-lowering in lower_call wasn't skipped.
                    REQUIRE(call->args.size() == 2);
                    found = true;
                }
            }
        }
    }
    REQUIRE(found);
}

TEST_CASE("static method definition has no `this` parameter in MIR",
          "[static][mir]") {
    auto r = frappe_test::compile(R"(
class Math {
    static fn add(a: Int, b: Int) -> Int { return a + b; }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "Math.add");
    REQUIRE(fn);

    // Static methods have only the declared params — no implicit `this`.
    REQUIRE(fn->parameters.size() == 2);
    REQUIRE(fn->locals[fn->parameters[0].index].name == "a");
    REQUIRE(fn->locals[fn->parameters[1].index].name == "b");
}