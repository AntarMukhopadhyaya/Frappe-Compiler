//
// Created by antar on 23/10/26.
//
// Regression tests for class subtyping in check_assignable. Before this
// round, `let d = new Dog(); care_for(d);` failed with "expected Animal,
// got Dog" even though Dog extends Animal. Since Frappe classes are
// reference types (LLVM `ptr`) and field layout is base-first, the fix
// is purely in the checker — codegen is unchanged.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

// ============================================================================
// Direct subtyping
// ============================================================================

TEST_CASE("subtyping: derived passed where base expected",
          "[subtyping]") {
    auto r = frappe_test::compile(R"(
class Animal { }
class Dog extends Animal { }
fn care(a: Animal) { }
fn main() {
    let d = new Dog();
    care(d);
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("subtyping: base passed where derived expected rejected",
          "[subtyping]") {
    auto r = frappe_test::compile(R"(
class Animal { }
class Dog extends Animal { }
fn pet(d: Dog) { }
fn main() {
    let a = new Animal();
    pet(a);
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("expected Dog, got Animal") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

// ============================================================================
// Multi-level chain
// ============================================================================

TEST_CASE("subtyping: grandchild satisfies grandparent parameter",
          "[subtyping][multi-level]") {
    auto r = frappe_test::compile(R"(
class A { }
class B extends A { }
class C extends B { }
fn use_a(a: A) { }
fn main() {
    let c = new C();
    use_a(c);
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("subtyping: middle class satisfies base parameter",
          "[subtyping][multi-level]") {
    auto r = frappe_test::compile(R"(
class A { }
class B extends A { }
class C extends B { }
fn use_a(a: A) { }
fn main() {
    let b = new B();
    use_a(b);
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Sibling classes — no common ancestor relationship
// ============================================================================

TEST_CASE("subtyping: sibling classes rejected",
          "[subtyping][negative]") {
    auto r = frappe_test::compile(R"(
class Base { }
class Cat extends Base { }
class Dog extends Base { }
fn pet_dog(d: Dog) { }
fn main() {
    let c = new Cat();
    pet_dog(c);
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("expected Dog, got Cat") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("subtyping: unrelated classes rejected", "[subtyping][negative]") {
    auto r = frappe_test::compile(R"(
class Cat { }
class Dog { }
fn pet_dog(d: Dog) { }
fn main() {
    let c = new Cat();
    pet_dog(c);
}
)");
    REQUIRE(r->has_errors());
}

// ============================================================================
// Return type subtyping
// ============================================================================

TEST_CASE("subtyping: derived-typed return used as base in assignment",
          "[subtyping][return]") {
    auto r = frappe_test::compile(R"(
class Animal { }
class Dog extends Animal { }
fn make_dog() -> Dog { return new Dog(); }
fn main() {
    let a: Animal = make_dog();
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Regression — existing nil → Optional path must keep working
// ============================================================================

TEST_CASE("subtyping: nil -> Optional<Base> still accepted after new branch",
          "[subtyping][regression]") {
    auto r = frappe_test::compile(R"(
class Animal { }
fn main() {
    let a: Animal? = nil;
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Field access through derived pointer still resolves after subtyping
// ============================================================================

TEST_CASE("subtyping: base field accessible through derived instance",
          "[subtyping][field-access]") {
    auto r = frappe_test::compile(R"(
class Base {
    public value: Int;
    fn init() { this.value = 0; }
}
class Derived extends Base {
    fn get() -> Int { return this.value; }
}
fn main() {
    let d = new Derived();
    let v = d.get();
}
)");
    CHECK_NO_ERRORS(r.get());
}