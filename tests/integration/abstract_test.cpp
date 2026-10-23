//
// Created by antar on 22/10/26.
//


#include <catch2/catch_test_macros.hpp>
#include "../helpers/compile.hpp"

// ============================================================================
// Rule 1 — abstract class cannot be instantiated
// ============================================================================

TEST_CASE("abstract class cannot be instantiated", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
fn main() {
    let s = new Shape();
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("cannot instantiate abstract class 'Shape'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("concrete class can be instantiated", "[abstract]") {
    auto r = frappe_test::compile(R"(
class Shape {
    fn area() -> Float { return 0.0; }
}
fn main() {
    let s = new Shape();
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Rules 2 & 3 — abstract fn placement and body
// ============================================================================

TEST_CASE("abstract method in concrete class errors", "[abstract]") {
    auto r = frappe_test::compile(R"(
class Foo {
    abstract fn bar() -> Int;
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("requires abstract class") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("abstract method with body errors", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Foo {
    abstract fn bar() -> Int { return 0; }
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("cannot have a body") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("abstract init rejected", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Foo {
    abstract fn init();
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("'init' cannot be abstract") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("concrete method in abstract class is fine", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    fn helper() -> Int { return 0; }
    abstract fn area() -> Float;
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Rule 4 — inherited abstract methods must be implemented
// ============================================================================

TEST_CASE("concrete class implementing abstract method is fine", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
class Circle extends Shape {
    fn area() -> Float { return 3.14; }
}
fn main() {
    let c = new Circle();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("concrete class missing abstract method errors", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
class Circle extends Shape {
}
fn main() {
    let c = new Circle();
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("must implement abstract method 'area'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("abstract method implemented with override keyword", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
class Circle extends Shape {
    override fn area() -> Float { return 3.14; }
}
fn main() {
    let c = new Circle();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("abstract class extending abstract can defer implementation",
          "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
abstract class NamedShape extends Shape {
    fn name() -> Int { return 0; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("abstract method inherited two levels up", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class A {
    abstract fn draw() -> Int;
}
abstract class B extends A { }
class C extends B { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("must implement abstract method 'draw'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}

TEST_CASE("abstract method implemented two levels up satisfies", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class A {
    abstract fn draw() -> Int;
}
abstract class B extends A {
    fn draw() -> Int { return 1; }
}
class C extends B { }
fn main() {
    let c = new C();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("abstract method satisfied by intermediate concrete override",
          "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class A {
    abstract fn draw() -> Int;
}
abstract class B extends A { }
class C extends B {
    fn draw() -> Int { return 1; }
}
fn main() {
    let c = new C();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("two abstract methods both must be implemented", "[abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
    abstract fn perimeter() -> Float;
}
class Circle extends Shape {
    fn area() -> Float { return 3.14; }
}
)");
    REQUIRE(r->has_errors());
    // At least one error for perimeter.
    bool found = false;
    for (const auto& e : r->errors) {
        if (e.message.find("must implement abstract method 'perimeter'") != std::string::npos) {
            found = true; break;
        }
    }
    REQUIRE(found);
}