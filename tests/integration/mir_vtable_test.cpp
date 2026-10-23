//
// Created by antar on 23/10/26.
//
//  MIR vtable metadata. Verifies that build_class computes
// vtable_slots and vtable_slot correctly for classes that implement
// interfaces. No codegen, no dispatch — those land in 5c. The metadata
// here is consumed by 5c to emit `@Class.vtable` globals.

#include <catch2/catch_test_macros.hpp>

#include <frappe/mir/builder.hpp>
#include <frappe/mir/module.hpp>

#include "../helpers/compile.hpp"

namespace {

    auto build_mir(frappe_test::Compile_Result *r) {
        return frappe::mir::build_module(
            r->program, &*r->checker, &r->engine, &r->arena);
    }

    const frappe::mir::MirClass *find_class(
        const frappe::mir::MirModule &m, std::string_view name) {
        for (const auto &c: m.classes) {
            if (c.name == name) return &c;
        }
        return nullptr;
    }

    const frappe::mir::MirMethod *find_method(
        const frappe::mir::MirClass *c, std::string_view name) {
        if (!c) return nullptr;
        for (const auto &m: c->methods) {
            if (m.name == name) return &m;
        }
        return nullptr;
    }

}

// ============================================================================
// Classes without interfaces — no vtable
// ============================================================================

TEST_CASE("vtable: class with no interfaces has empty vtable", "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
class Point { public x: Int; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Point");
    REQUIRE(c);
    CHECK_FALSE(c->has_vtable);
    // total_interface_slots = 0, so vtable_slots should be empty
    // (or all-empty with size 0).
    for (const auto &s: c->vtable_slots) {
        CHECK(s.empty());
    }
}

TEST_CASE("vtable: class without interfaces when others have them",
          "[mir][vtable]") {
    // Point doesn't implement anything, but Speaker exists in the program,
    // so total_interface_slots > 0. Point's vtable_slots is sized to that
    // but all entries are empty (no slots filled).
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
class Point { public x: Int; }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *p = find_class(mir, "Point");
    REQUIRE(p);
    CHECK_FALSE(p->has_vtable);
    for (const auto &s: p->vtable_slots) {
        CHECK(s.empty());
    }
}

// ============================================================================
// Single interface — vtable populated
// ============================================================================

TEST_CASE("vtable: class implementing one interface has populated vtable",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    CHECK(c->has_vtable);
    REQUIRE(c->vtable_slots.size() == 1);
    CHECK(c->vtable_slots[0] == "Dog.speak");
}

TEST_CASE("vtable: multi-method interface produces multi-slot vtable",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Animal {
    fn speak() -> Int;
    fn eat() -> Int;
}
class Dog implements Animal {
    fn speak() -> Int { return 1; }
    fn eat() -> Int { return 2; }
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    REQUIRE(c->vtable_slots.size() == 2);
    CHECK(c->vtable_slots[0] == "Dog.speak");  // Animal.speak → slot 0
    CHECK(c->vtable_slots[1] == "Dog.eat");    // Animal.eat → slot 1
}

// ============================================================================
// Multiple interfaces — global slot assignment
// ============================================================================

TEST_CASE("vtable: class implementing multiple interfaces fills both slot ranges",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
interface Eater   { fn eat() -> Int; }
class Dog implements Speaker, Eater {
    fn speak() -> Int { return 1; }
    fn eat() -> Int { return 2; }
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    REQUIRE(c->vtable_slots.size() == 2);
    CHECK(c->vtable_slots[0] == "Dog.speak");  // Speaker.speak → slot 0
    CHECK(c->vtable_slots[1] == "Dog.eat");    // Eater.eat → slot 1
}

TEST_CASE("vtable: global slots unique across interfaces",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
interface Eater   { fn eat() -> Int; }
class Dog implements Eater {
    fn eat() -> Int { return 2; }
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    // Speaker.speak = slot 0 (unused by Dog), Eater.eat = slot 1.
    REQUIRE(c->vtable_slots.size() == 2);
    CHECK(c->vtable_slots[0].empty());         // not implemented by Dog
    CHECK(c->vtable_slots[1] == "Dog.eat");
}

// ============================================================================
// Inheritance — inherited methods still fill slots
// ============================================================================

TEST_CASE("vtable: inherited method fills interface slot in derived class",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Animal implements Speaker { fn speak() -> Int { return 0; } }
class Dog extends Animal { }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    CHECK(c->has_vtable);
    REQUIRE(c->vtable_slots.size() == 1);
    // Dog inherits Animal.speak — that's the fn_name in the slot.
    CHECK(c->vtable_slots[0] == "Animal.speak");
}

TEST_CASE("vtable: override still fills the interface slot",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Animal implements Speaker { fn speak() -> Int { return 0; } }
class Dog extends Animal { override fn speak() -> Int { return 1; } }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    REQUIRE(c->vtable_slots.size() == 1);
    // Dog.speak overrides — the slot points at Dog's implementation.
    CHECK(c->vtable_slots[0] == "Dog.speak");
}

// ============================================================================
// MirMethod::vtable_slot
// ============================================================================

TEST_CASE("vtable: MirMethod::vtable_slot reflects interface assignment",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    auto *m = find_method(c, "speak");
    REQUIRE(m);
    CHECK(m->vtable_slot.has_value());
    CHECK(*m->vtable_slot == 0);
}

TEST_CASE("vtable: non-interface method has no vtable_slot",
          "[mir][vtable]") {
    auto r = frappe_test::compile(R"(
class Dog {
    fn bark() -> Int { return 1; }
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *c = find_class(mir, "Dog");
    REQUIRE(c);
    auto *m = find_method(c, "bark");
    REQUIRE(m);
    CHECK_FALSE(m->vtable_slot.has_value());
}