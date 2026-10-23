//
// Created by antar on 22/10/26.
//

#include <catch2/catch_test_macros.hpp>
#include "../helpers/compile.hpp"

// ============================================================================
// final class
// ============================================================================

TEST_CASE("final class cannot be extended", "[final]") {
    auto r = frappe_test::compile(R"(
final class Parent { }
class Child extends Parent { }
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "cannot extend final class");
}

TEST_CASE("non-final class can be extended", "[final]") {
    auto r = frappe_test::compile(R"(
class Parent { }
class Child extends Parent { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("three-level chain, middle is final", "[final]") {
    auto r = frappe_test::compile(R"(
class A { }
final class B extends A { }
class C extends B { }
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 4, "cannot extend final class 'B'");
}

// ============================================================================
// final method
// ============================================================================

TEST_CASE("final method cannot be overridden", "[final]") {
    auto r = frappe_test::compile(R"(
class Parent {
    final fn draw() { }
}
class Child extends Parent {
    override fn draw() { }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 6, "cannot override final method 'draw'");
}

TEST_CASE("non-final method can be overridden", "[final]") {
    auto r = frappe_test::compile(R"(
class Parent {
    fn draw() { }
}
class Child extends Parent {
    override fn draw() { }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("final method inherited two levels up cannot be overridden",
          "[final]") {
    auto r = frappe_test::compile(R"(
class A {
    final fn draw() { }
}
class B extends A { }
class C extends B {
    override fn draw() { }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot override final method 'draw'");
}

TEST_CASE("method cannot be both final and abstract", "[final]") {
    auto r = frappe_test::compile(R"(
abstract class Foo {
    final abstract fn bar() -> Int;
}
)");
    REQUIRE(r->has_errors());
    // Message may fire from Rule 3.
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("both 'final' and 'abstract'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

// ============================================================================
// final field — assignment
// ============================================================================

TEST_CASE("final field can be assigned in declaring class init", "[final]") {
    auto r = frappe_test::compile(R"(
class Point {
    final x: Float;
    final y: Float;
    fn init(x: Float, y: Float) {
        this.x = x;
        this.y = y;
    }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("final field cannot be assigned outside init", "[final]") {
    auto r = frappe_test::compile(R"(
class Point {
    final x: Float;
    fn init(x: Float) { this.x = x; }
    fn set(v: Float) { this.x = v; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 5, "final field 'x' can only be assigned in 'init'");
}

TEST_CASE("final field cannot be assigned from a subclass init", "[final]") {
    auto r = frappe_test::compile(R"(
class Point {
    final x: Float;
    fn init(x: Float) { this.x = x; }
}
class Sub extends Point {
    fn init(x: Float) { this.x = x; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "outside declaring class 'Point'");
}

TEST_CASE("final field compound assignment rejected", "[final]") {
    auto r = frappe_test::compile(R"(
class Counter {
    final n: Int;
    fn init(n: Int) { this.n = n; }
    fn bump() { this.n += 1; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 5, "final field 'n' can only be assigned in 'init'");
}

TEST_CASE("final field assignment via field access chain rejected", "[final]") {
    auto r = frappe_test::compile(R"(
class Point {
    final x: Float;
    fn init(x: Float) { this.x = x; }
}
fn main() {
    let p = new Point(1.0);
    p.x = 5.0;
}
)");
    REQUIRE(r->has_errors());
    // Should fire "outside declaring class" (p is not `this` in declaring class).
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("outside declaring class") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

// ============================================================================
// final field — initialization requirement
// ============================================================================

TEST_CASE("final field with declaration-site init is fine", "[final]") {
    auto r = frappe_test::compile(R"(
class Constants {
    final PI: Float = 3.14159;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("final field with no init and no class init errors", "[final]") {
    auto r = frappe_test::compile(R"(
class Broken {
    final x: Float;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "has no initializer and class has no 'init'");
}

TEST_CASE("final field with no init but class has init is accepted", "[final]") {
    // v1 limitation — we don't verify init actually assigns the field.
    // The presence of `init` is enough.
    auto r = frappe_test::compile(R"(
class Foo {
    final x: Float;
    fn init() { }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("non-final field with no init is fine", "[final]") {
    auto r = frappe_test::compile(R"(
class Foo {
    x: Float;
}
)");
    CHECK_NO_ERRORS(r.get());
}