//
// Created by antar on 23/10/26.
//

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

namespace {

    bool has_error_containing(const frappe_test::Compile_Result *r,
                              std::string_view needle) {
        for (const auto &e : r->errors) {
            if (e.message.find(needle) != std::string::npos) return true;
        }
        return false;
    }

}

// ============================================================================
// Positive cases
// ============================================================================

TEST_CASE("interface: class with matching method passes", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: extra methods allowed", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker {
    fn speak() -> Int { return 1; }
    fn bark() { }
}
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: multiple interfaces all satisfied", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
interface Eater { fn eat(); }
class Dog implements Speaker, Eater {
    fn speak() -> Int { return 1; }
    fn eat() { }
}
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: empty marker interface accepted", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Marker { }
class Dog implements Marker { }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Negative — missing or wrong implementation
// ============================================================================

TEST_CASE("interface: class with no implementation errors", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(),
          "does not implement interface 'Speaker' method 'speak'"));
}

TEST_CASE("interface: wrong return type errors", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> String { return "woof"; } }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "incompatible signature"));
}

TEST_CASE("interface: wrong param type errors", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Eater { fn eat(food: String); }
class Dog implements Eater { fn eat(food: Int) { } }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "incompatible signature"));
}

TEST_CASE("interface: wrong param count errors", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Eater { fn eat(food: String); }
class Dog implements Eater { fn eat(a: String, b: Int) { } }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "incompatible signature"));
}

TEST_CASE("interface: multiple interfaces, one missing", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
interface Eater { fn eat(); }
class Dog implements Speaker, Eater {
    fn speak() -> Int { return 1; }
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(),
          "does not implement interface 'Eater' method 'eat'"));
}

// ============================================================================
// Negative — malformed implements clause
// ============================================================================

TEST_CASE("interface: implements a class (not interface) errors", "[interface]") {
    auto r = frappe_test::compile(R"(
class Foo { }
class Dog implements Foo { }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "'Foo' is not an interface"));
}

TEST_CASE("interface: duplicate implements errors", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker, Speaker {
    fn speak() -> Int { return 1; }
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "more than once"));
}

// ============================================================================
// Negative — invalid interface declarations
// ============================================================================

TEST_CASE("interface: inheritance rejected", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Animal { fn breathe(); }
interface Pet extends Animal { fn play(); }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "interface inheritance not supported in v1"));
}

TEST_CASE("interface: method with body rejected", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int { return 0; } }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "cannot have a body"));
}

TEST_CASE("interface: static method rejected", "[interface]") {
    auto r = frappe_test::compile(R"(
interface Speaker { static fn speak() -> Int; }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(), "cannot be static"));
}

// ============================================================================
// Abstract class interaction
// ============================================================================

TEST_CASE("interface: abstract class may defer implementation",
          "[interface][abstract]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
abstract class Animal implements Speaker { }
class Dog extends Animal { fn speak() -> Int { return 1; } }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: concrete subclass of abstract must implement",
          "[interface][abstract]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
abstract class Animal implements Speaker { }
class Dog extends Animal { }
fn main() { }
)");
    REQUIRE(r->has_errors());
    CHECK(has_error_containing(r.get(),
          "does not implement interface 'Speaker' method 'speak'"));
}


// ============================================================================
// Interface-typed variables
// ============================================================================

TEST_CASE("interface: class instance assigned to interface variable",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() {
    let s: Speaker = new Dog();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: non-implementing class assigned to interface variable fails",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Cat { }
fn main() {
    let s: Speaker = new Cat();
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("expected Speaker, got Cat") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("interface: derived class inherits interface compatibility",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Animal implements Speaker { fn speak() -> Int { return 0; } }
class Dog extends Animal { }
fn main() {
    let s: Speaker = new Dog();
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Interface-typed function parameters
// ============================================================================

TEST_CASE("interface: implementing class passed to interface-typed parameter",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn announce(s: Speaker) { }
fn main() {
    announce(new Dog());
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: non-implementing class passed to interface-typed parameter fails",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Cat { }
fn announce(s: Speaker) { }
fn main() {
    announce(new Cat());
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("expected Speaker, got Cat") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Interface-typed return types
// ============================================================================

TEST_CASE("interface: implementing class returned from interface-typed function",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn make() -> Speaker { return new Dog(); }
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: non-implementing class returned from interface function fails",
          "[interface][types]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Cat { }
fn make() -> Speaker { return new Cat(); }
fn main() { }
)");
    REQUIRE(r->has_errors());
}

// ============================================================================
// Interface method calls (checker-only — MIR dispatch is later)
// ============================================================================

TEST_CASE("interface: method call on interface-typed value type-checks",
          "[interface][types][methods]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn call_speak(s: Speaker) -> Int {
    return s.speak();
}
fn main() { }
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("interface: unknown method on interface errors",
          "[interface][types][methods]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn call_speak(s: Speaker) -> Int {
    return s.missing();
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("has no method 'missing'") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("interface: wrong arg count on interface method errors",
          "[interface][types][methods]") {
    auto r = frappe_test::compile(R"(
interface Eater { fn eat(food: String); }
class Dog implements Eater { fn eat(food: String) { } }
fn feed(e: Eater) { e.eat(); }
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("expects 1 arguments, got 0") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Field access on interface values must fail
// ============================================================================

TEST_CASE("interface: field access on interface value errors",
          "[interface][types][fields]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker {
    public x: Int;
    fn speak() -> Int { return 1; }
}
fn try_field(s: Speaker) -> Int {
    return s.x;
}
fn main() { }
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("interface or enum") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Abstract intermediate class
// ============================================================================

TEST_CASE("interface: abstract class implementing interface, concrete leaf assigned",
          "[interface][types][abstract]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
abstract class Animal implements Speaker { }
class Dog extends Animal { fn speak() -> Int { return 1; } }
fn main() {
    let s: Speaker = new Dog();
}
)");
    CHECK_NO_ERRORS(r.get());
}