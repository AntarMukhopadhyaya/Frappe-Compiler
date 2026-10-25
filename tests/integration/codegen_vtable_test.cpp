//
// Created by antar on 24/10/26.
//
// vtable emission and virtual dispatch in codegen. Verifies
// the emitted IR for interface-typed calls — struct layout with vptr,
// vtable global, vptr initialization in `new`, indirect dispatch through
// the vtable slot.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/codegen_test.hpp"

namespace {
    struct IR_Result {
        std::unique_ptr<frappe_test::Compile_Result> compile;
        std::optional<std::string> ir;
        bool skip = false;
    };

    IR_Result compile_to_ir(std::string_view src) {
        IR_Result out;
        out.compile = frappe_test::compile(src);
        if (out.compile->has_errors()) return out;
        out.ir = frappe_test::build_ir(*out.compile);
        if (!out.ir.has_value()) out.skip = true;
        return out;
    }
}

// ============================================================================
// Struct layout — vptr at field 0
// ============================================================================

TEST_CASE("vtable-ir: class without interfaces has vptr in struct layout",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
class Point { public x: Int; }
fn main() {
    let p = new Point();
}
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "%Point = type { ptr, i64 }"));
}

TEST_CASE("vtable-ir: class with interface has vptr in struct layout",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
interface Speaker { fn speak() -> Int; }

class Dog implements Speaker {
    public name: Int;
    fn speak() -> Int { return 1; }
}

fn main() {
    let dog = new Dog();
}
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "%Dog = type { ptr, i64 }"));
}

// ============================================================================
// Vtable global emission
// ============================================================================

TEST_CASE("vtable-ir: interface-implementing class emits vtable global",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.vtable"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "constant [1 x ptr]"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.speak"));    // ← no quotes
}

TEST_CASE("vtable-ir: class without interface has no vtable global",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
class Point { public x: Int; }
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, "@Point.vtable"));
}

TEST_CASE("vtable-ir: multi-method interface produces multi-entry vtable",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
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
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.vtable"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "constant [2 x ptr]"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.speak"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.eat"));
}

// ============================================================================
// vptr initialization
// ============================================================================

TEST_CASE("vtable-ir: new stores vtable pointer at vptr slot",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn main() { let d = new Dog(); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    // Vptr store must be present, pointing at the class vtable.
    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.vtable"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "vptr_slot"));
}

TEST_CASE("vtable-ir: new for class without interface stores null vptr",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
class Point { public x: Int; }
fn main() { let p = new Point(); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    // No vtable global, and vptr_slot still gets written (with null).
    REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, "@Point.vtable"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "vptr_slot"));
}

// ============================================================================
// Virtual dispatch
// ============================================================================

TEST_CASE("vtable-ir: interface call emits vptr load, GEP, indirect call",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
interface Speaker { fn speak() -> Int; }
class Dog implements Speaker { fn speak() -> Int { return 1; } }
fn call_speak(s: Speaker) -> Int {
    return s.speak();
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    // The dispatch pattern: load vptr, gep slot, load fn, indirect call.
    REQUIRE(frappe_test::ir_contains(*r.ir, "vptr"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "vslot"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "vfn"));
    // Indirect call through a function pointer — LLVM prints this as
    // `call <ret> %vfn(...)` rather than `call <ret> @name(...)`.
    REQUIRE(frappe_test::ir_contains(*r.ir, "call i64 %"));
}

TEST_CASE("vtable-ir: non-interface method call remains direct",
          "[vtable][codegen]") {
    auto r = compile_to_ir(R"(
class Dog { fn speak() -> Int { return 1; } }
fn main() {
    let d = new Dog();
    let x = d.speak();
}
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    // Direct call to @Dog.speak, not indirect.
    REQUIRE(frappe_test::ir_contains(*r.ir, "@Dog.speak"));

    // No virtual dispatch: no vfn slot load, no indirect call.
    // (The '%vptr_slot' in the `new` path is the null vptr store from
    // the uniform-layout design — it appears for every class, not just
    // interface implementors. It's not virtual dispatch.)
    REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, "vfn"));
    REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, "call i64 %"));
}


// ============================================================================
// Regression: existing class tests still pass
// ============================================================================

TEST_CASE("vtable-ir: field access through vtable-bearing class uses shifted index",
          "[vtable][codegen][regression]") {
    auto r = compile_to_ir(R"(
class Point {
    public x: Int;
    fn get() -> Int { return this.x; }
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built"); return; }

    // GEP must use index 1 (vptr at 0, x at 1).
    REQUIRE(frappe_test::ir_contains(*r.ir, "getelementptr inbounds nuw %Point, ptr"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "i32 0, i32 1"));
}