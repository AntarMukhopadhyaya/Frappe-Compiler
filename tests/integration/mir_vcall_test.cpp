//
// Created by antar on 24/10/26.
//
//  virtual call detection in MIR. Interface method calls must
// set `is_virtual = true` and populate `vtable_slot` from the global
// interface_slot_map built in Round 5b. Non-interface calls keep
// `is_virtual = false`. Codegen hasn't been taught dispatch yet (Round
// 5c3) — a virtual call in a compiled program must error cleanly, not
// crash or emit garbage.

#include <catch2/catch_test_macros.hpp>

#include <frappe/mir/builder.hpp>
#include <frappe/mir/instruction.hpp>
#include <frappe/mir/module.hpp>

#include "../helpers/compile.hpp"

namespace {
    auto build_mir(frappe_test::Compile_Result *r) {
        return frappe::mir::build_module(
            r->program, &*r->checker, &r->engine, &r->arena);
    }

    const frappe::mir::MirFunction *find_fn(
        const frappe::mir::MirModule &m, std::string_view name) {
        for (const auto &fn: m.functions) {
            if (fn.name == name) return &fn;
        }
        return nullptr;
    }
}

// ============================================================================
// Virtual call detection
// ============================================================================

TEST_CASE("vcall: interface method call sets is_virtual and vtable_slot",
          "[mir][vcall]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() {
    let s: Speaker = new Dog();
    let x = s.speak();
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = find_fn(mir, "main");
    REQUIRE(fn);

    bool found = false;
    for (const auto &inst: fn->instructions) {
        if (auto *call = std::get_if<frappe::mir::MirCall>(&inst)) {
            if (call->is_virtual) {
                found = true;
                REQUIRE(call->vtable_slot.has_value());
                CHECK(*call->vtable_slot == 0);  // Speaker.speak → slot 0
                CHECK(call->args.size() == 1);   // receiver only
                break;
            }
        }
    }
    CHECK(found);
}

TEST_CASE("vcall: regular class method call has is_virtual=false",
          "[mir][vcall]") {
    auto r = frappe_test::compile(R"(
class Dog { fn speak() -> Int { return 1; } }
fn main() {
    let d = new Dog();
    let x = d.speak();
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = find_fn(mir, "main");
    REQUIRE(fn);

    bool found_non_virtual_call = false;
    for (const auto &inst: fn->instructions) {
        if (auto *call = std::get_if<frappe::mir::MirCall>(&inst)) {
            CHECK_FALSE(call->is_virtual);
            CHECK_FALSE(call->vtable_slot.has_value());
            found_non_virtual_call = true;
        }
    }
    CHECK(found_non_virtual_call);
}

TEST_CASE("vcall: multiple interfaces produce distinct slots",
          "[mir][vcall]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
interface Eater   { fn eat() -> Int; }
class Dog implements Speaker, Eater {
    fn speak() -> Int { return 1; }
    fn eat() -> Int { return 2; }
}
fn main() {
    let s: Speaker = new Dog();
    let e: Eater = new Dog();
    let x = s.speak();
    let y = e.eat();
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = find_fn(mir, "main");
    REQUIRE(fn);

    std::optional<std::uint32_t> speak_slot;
    std::optional<std::uint32_t> eat_slot;
    for (const auto &inst: fn->instructions) {
        if (auto *call = std::get_if<frappe::mir::MirCall>(&inst)) {
            if (!call->is_virtual) continue;
            auto *dc = std::get_if<frappe::mir::MirDirectCallee>(&call->callee);
            if (!dc) continue;
            if (dc->name == "speak") speak_slot = call->vtable_slot;
            if (dc->name == "eat")   eat_slot   = call->vtable_slot;
        }
    }
    REQUIRE(speak_slot.has_value());
    REQUIRE(eat_slot.has_value());
    CHECK(*speak_slot != *eat_slot);
}

TEST_CASE("vcall: virtual call with arguments has receiver-first ordering",
          "[mir][vcall]") {
    auto r = frappe_test::compile(R"(
interface Eater { fn eat(food: String); }
class Dog implements Eater { fn eat(food: String) { } }
fn main() {
    let e: Eater = new Dog();
    e.eat("bone");
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = find_fn(mir, "main");
    REQUIRE(fn);

    bool found = false;
    for (const auto &inst: fn->instructions) {
        if (auto *call = std::get_if<frappe::mir::MirCall>(&inst)) {
            if (call->is_virtual) {
                found = true;
                CHECK(call->args.size() == 2);        // receiver + 1 arg
                CHECK_FALSE(call->destination.has_value());  // void return
                break;
            }
        }
    }
    CHECK(found);
}

TEST_CASE("vcall: repeated calls to same interface method share slot",
          "[mir][vcall]") {
    auto r = frappe_test::compile(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn call_twice(s: Speaker) -> Int {
    let a = s.speak();
    let b = s.speak();
    return a + b;
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    auto *fn = find_fn(mir, "call_twice");
    REQUIRE(fn);

    std::pmr::vector<std::optional<std::uint32_t>> slots{mir.functions.get_allocator().resource()};
    for (const auto &inst: fn->instructions) {
        if (auto *call = std::get_if<frappe::mir::MirCall>(&inst)) {
            if (call->is_virtual) slots.push_back(call->vtable_slot);
        }
    }
    REQUIRE(slots.size() == 2);
    REQUIRE(slots[0].has_value());
    REQUIRE(slots[1].has_value());
    CHECK(*slots[0] == *slots[1]);
}


// ============================================================================
// Sanity: programs without interface calls remain unaffected
// ============================================================================

TEST_CASE("vcall: non-interface program unaffected", "[mir][vcall]") {
    auto r = frappe_test::compile(R"(
class Dog { fn speak() -> Int { return 1; } }
fn main() {
    let d = new Dog();
    let x = d.speak();
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mir = build_mir(r.get());
    for (const auto &fn: mir.functions) {
        for (const auto &inst: fn.instructions) {
            if (auto *call = std::get_if<frappe::mir::MirCall>(&inst)) {
                CHECK_FALSE(call->is_virtual);
                CHECK_FALSE(call->vtable_slot.has_value());
            }
        }
    }
}