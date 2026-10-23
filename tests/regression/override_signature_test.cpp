//
// Created by antar on 23/10/26.
//
// Regression guard for name-only override/abstract checks. Prior to the fix,
// a method with the right name but wrong signature silently "satisfied"
// an override or abstract requirement — a real correctness bug that would
// turn into silent miscompilation once vtables land.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

// ============================================================================
// Override — signature checking
// ============================================================================

TEST_CASE("override: matching signature accepted", "[override][signature]") {
    auto r = frappe_test::compile(R"(
class Animal {
    fn speak() -> Int { return 0; }
}
class Dog extends Animal {
    override fn speak() -> Int { return 1; }
}
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("override: wrong return type rejected", "[override][signature]") {
    auto r = frappe_test::compile(R"(
class Animal {
    fn speak() -> Float { return 0.0; }
}
class Dog extends Animal {
    override fn speak() -> Int { return 1; }
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("incompatible signature") != std::string::npos
            && e.message.find("speak") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

TEST_CASE("override: wrong param count rejected", "[override][signature]") {
    auto r = frappe_test::compile(R"(
class Animal {
    fn feed(amount: Int) { }
}
class Dog extends Animal {
    override fn feed(amount: Int, extra: Int) { }
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("incompatible signature") != std::string::npos
            && e.message.find("feed") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

TEST_CASE("override: wrong param type rejected", "[override][signature]") {
    auto r = frappe_test::compile(R"(
class Animal {
    fn feed(amount: Int) { }
}
class Dog extends Animal {
    override fn feed(amount: String) { }
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("incompatible signature") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

TEST_CASE("override: abstract parent method with wrong signature rejected",
          "[override][signature][abstract]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
class Circle extends Shape {
    override fn area() -> Int { return 0; }
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("incompatible signature") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Abstract implementation — signature checking
// ============================================================================

TEST_CASE("abstract: concrete impl with matching signature accepted",
          "[abstract][signature]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
class Circle extends Shape {
    fn area() -> Float { return 3.14; }
}
fn main() { let c = new Circle(); }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("abstract: concrete impl with wrong return type rejected",
          "[abstract][signature]") {
    auto r = frappe_test::compile(R"(
abstract class Shape {
    abstract fn area() -> Float;
}
class Circle extends Shape {
    fn area() -> Int { return 0; }
}
fn main() { let c = new Circle(); }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("does not match") != std::string::npos
            && e.message.find("area") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

TEST_CASE("abstract: concrete impl with wrong param type rejected",
          "[abstract][signature]") {
    auto r = frappe_test::compile(R"(
abstract class Animal {
    abstract fn feed(amount: Int);
}
class Dog extends Animal {
    fn feed(amount: String) { }
}
fn main() { let d = new Dog(); }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("does not match") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Multi-level inheritance — signature check follows the whole chain
// ============================================================================

TEST_CASE("abstract: intermediate class implements correctly, derived inherits",
          "[abstract][signature][multi-level]") {
    auto r = frappe_test::compile(R"(
abstract class A {
    abstract fn draw() -> Int;
}
class B extends A {
    fn draw() -> Int { return 1; }
}
class C extends B { }
fn main() { let c = new C(); }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("abstract: intermediate class implements wrong signature, derived caught",
          "[abstract][signature][multi-level]") {
    auto r = frappe_test::compile(R"(
abstract class A {
    abstract fn draw() -> Int;
}
class B extends A {
    fn draw() -> String { return "x"; }
}
class C extends B { }
fn main() { let c = new C(); }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("does not match") != std::string::npos
            && e.message.find("draw") != std::string::npos) {
            found = true; break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Sanity: non-override, non-abstract redefinition stays permissive
// ============================================================================

TEST_CASE("non-override redefinition with different signature is permitted "
          "(method hiding, not overriding)",
          "[override][signature]") {
    // A derived class can define a method with the same name as a parent
    // method without `override` — this is "method hiding," a valid pattern
    // in C++, Java, C#, etc. Only `override` and abstract-implementation
    // paths enforce signature compatibility.
    auto r = frappe_test::compile(R"(
class Animal {
    fn speak() -> Float { return 0.0; }
}
class Dog extends Animal {
    fn speak() -> Int { return 1; }
}
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}