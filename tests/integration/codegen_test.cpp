//
// Created by antar on 22/10/26.
//

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

TEST_CASE("codegen: empty main emits IR with target info", "[codegen]") {
    auto r = compile_to_ir("fn main() { }");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::has_target_triple(*r.ir));
    REQUIRE(frappe_test::has_data_layout(*r.ir));
}

TEST_CASE("codegen: empty main defines both user and wrapper functions",
          "[codegen]") {
    auto r = compile_to_ir("fn main() { }");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_defines_function(*r.ir, "__frappe_user_main"));
    REQUIRE(frappe_test::ir_defines_function(*r.ir, "main"));
}

TEST_CASE("codegen: runtime functions are declared, not defined",
          "[codegen]") {
    auto r = compile_to_ir("fn main() { }");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_declares_function(*r.ir, "__frappe_alloc"));
    REQUIRE(frappe_test::ir_declares_function(*r.ir, "__frappe_alloc_array"));
    REQUIRE(frappe_test::ir_declares_function(*r.ir, "__frappe_panic"));
}

TEST_CASE("codegen: return constant emits ret i64", "[codegen]") {
    auto r = compile_to_ir("fn main() -> Int { return 42; }");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_contains(*r.ir, "ret i64 42"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "trunc i64"));
}

TEST_CASE("codegen: return arithmetic emits add", "[codegen]") {
    auto r = compile_to_ir("fn main() -> Int { return 1 + 2; }");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_contains(*r.ir, "add i64"));
}

TEST_CASE("codegen: integer arithmetic chain emits multiple adds",
          "[codegen]") {
    auto r = compile_to_ir("fn main() -> Int { return 1 + 2 + 3; }");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_count(*r.ir, "add i64") >= 2);
}

TEST_CASE("codegen: string literal emits private global", "[codegen]") {
    auto r = compile_to_ir(R"(fn main() { let s = "hello"; })");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_contains(*r.ir, "private unnamed_addr constant"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "hello"));
}

TEST_CASE("codegen: if statement emits conditional branch", "[codegen]") {
    auto r = compile_to_ir(R"(
fn main(c: Bool) {
    if (c) { }
}
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_contains(*r.ir, "br i1"));
}

TEST_CASE("codegen: while loop emits icmp and branch", "[codegen]") {
    auto r = compile_to_ir(R"(
fn main(n: Int) -> Int {
    var i: Int = 0;
    while (i < n) {
        i = i + 1;
    }
    return i;
}
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_contains(*r.ir, "br i1"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "icmp"));
}

TEST_CASE("codegen: class with fields emits a struct type", "[codegen]") {
    auto r = compile_to_ir(R"(
class Point {
    public x: Int;
    public y: Int;
    fn init(a: Int, b: Int) {
        this.x = a;
        this.y = b;
    }
}
fn main() { let p = new Point(1, 2); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_contains(*r.ir, "%Point = type"));
}

TEST_CASE("codegen: new expression calls class init", "[codegen]") {
    auto r = compile_to_ir(R"(
class Point {
    public x: Int;
    fn init() { this.x = 0; }
}
fn main() { let p = new Point(); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_defines_function(*r.ir, "Point.init"));
    REQUIRE(frappe_test::ir_count(*r.ir, "call ptr @__frappe_alloc") >= 1);
}

TEST_CASE("codegen: generic instantiation mangles function name",
          "[codegen]") {
    auto r = compile_to_ir(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() -> Int { return identity(7); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) {
        SUCCEED("codegen not built (FRAPPE_LLVM=OFF)");
        return;
    }

    REQUIRE(frappe_test::ir_defines_function(*r.ir, "identity$Int"));
}

TEST_CASE("codegen: subtraction emits sub i64", "[codegen]") {
    CHECK_IR(R"(
fn sub(a: Int, b: Int) -> Int {
    return a - b;
}
fn main() { }
)", "sub i64");
}

TEST_CASE("codegen: multiplication emits mul i64", "[codegen]") {
    CHECK_IR(R"(
fn mul(a: Int, b: Int) -> Int {
    return a * b;
}
fn main() { }
)", "mul i64");
}

TEST_CASE("codegen: signed division emits sdiv i64", "[codegen]") {
    // Frappe Int is signed i64. If codegen ever regressed to udiv, negative
    // division would silently produce wrong results.
    CHECK_IR(R"(
fn div(a: Int, b: Int) -> Int {
    return a / b;
}
fn main() { }
)", "sdiv i64");
}

TEST_CASE("codegen: modulo emits srem i64", "[codegen]") {
    CHECK_IR(R"(
fn mod(a: Int, b: Int) -> Int {
    return a % b;
}
fn main() { }
)", "srem i64");
}


TEST_CASE("codegen: equality emits icmp eq", "[codegen]") {
    CHECK_IR(R"(
fn eq(a: Int, b: Int) -> Bool {
    return a == b;
}
fn main() { }
)", "icmp eq i64");
}

TEST_CASE("codegen: inequality emits icmp ne", "[codegen]") {
    CHECK_IR(R"(
fn ne(a: Int, b: Int) -> Bool {
    return a != b;
}
fn main() { }
)", "icmp ne i64");
}

TEST_CASE("codegen: less-than emits signed icmp slt", "[codegen]") {
    CHECK_IR(R"(
fn lt(a: Int, b: Int) -> Bool {
    return a < b;
}
fn main() { }
)", "icmp slt i64");
}

TEST_CASE("codegen: less-equal emits signed icmp sle", "[codegen]") {
    CHECK_IR(R"(
fn le(a: Int, b: Int) -> Bool {
    return a <= b;
}
fn main() { }
)", "icmp sle i64");
}

TEST_CASE("codegen: greater-than emits signed icmp sgt", "[codegen]") {
    CHECK_IR(R"(
fn gt(a: Int, b: Int) -> Bool {
    return a > b;
}
fn main() { }
)", "icmp sgt i64");
}

TEST_CASE("codegen: greater-equal emits signed icmp sge", "[codegen]") {
    CHECK_IR(R"(
fn ge(a: Int, b: Int) -> Bool {
    return a >= b;
}
fn main() { }
)", "icmp sge i64");
}

TEST_CASE("codegen: unary neg emits sub i64 0, x", "[codegen]") {
    // CreateNeg canonical form on i64 is `sub i64 0, %x`. If it ever
    // switched to something else, this test catches it.
    CHECK_IR(R"(
fn neg(x: Int) -> Int {
    return -x;
}
fn main() { }
)", "sub i64 0, %");
}

TEST_CASE("codegen: unary not emits xor i1", "[codegen]") {
    CHECK_IR(R"(
fn not_bool(x: Bool) -> Bool {
    return !x;
}
fn main() { }
)", "xor i1");
}


TEST_CASE("codegen: float addition emits fadd double", "[codegen]") {
    CHECK_IR(R"(
fn fadd(a: Float, b: Float) -> Float {
    return a + b;
}
fn main() { }
)", "fadd double");
}

TEST_CASE("codegen: float subtraction emits fsub double", "[codegen]") {
    CHECK_IR(R"(
fn fsub(a: Float, b: Float) -> Float {
    return a - b;
}
fn main() { }
)", "fsub double");
}

TEST_CASE("codegen: float multiplication emits fmul double", "[codegen]") {
    CHECK_IR(R"(
fn fmul(a: Float, b: Float) -> Float {
    return a * b;
}
fn main() { }
)", "fmul double");
}

TEST_CASE("codegen: float division emits fdiv double", "[codegen]") {
    CHECK_IR(R"(
fn fdiv(a: Float, b: Float) -> Float {
    return a / b;
}
fn main() { }
)", "fdiv double");
}

TEST_CASE("codegen: float comparison emits fcmp olt", "[codegen]") {
    CHECK_IR(R"(
fn flt(a: Float, b: Float) -> Bool {
    return a < b;
}
fn main() { }
)", "fcmp olt");
}

TEST_CASE("codegen: float equality emits fcmp oeq", "[codegen]") {
    CHECK_IR(R"(
fn feq(a: Float, b: Float) -> Bool {
    return a == b;
}
fn main() { }
)", "fcmp oeq");
}

TEST_CASE("codegen: && lowers to branches, not and i1", "[codegen]") {
    auto r = compile_to_ir(R"(
fn land(a: Bool, b: Bool) -> Bool {
    return a && b;
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    // Must emit a conditional branch for short-circuit evaluation.
    REQUIRE(frappe_test::ir_contains(*r.ir, "br i1"));
    // Must NOT emit a raw bitwise and — that would evaluate both operands
    // unconditionally, breaking short-circuit semantics.
    REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, "and i1"));
}

TEST_CASE("codegen: || lowers to branches, not or i1", "[codegen]") {
    auto r = compile_to_ir(R"(
fn lor(a: Bool, b: Bool) -> Bool {
    return a || b;
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "br i1"));
    REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, "or i1"));
}

TEST_CASE("codegen: string add calls __frappe_str_concat", "[codegen]") {
    CHECK_IR(R"(
fn cat(a: String, b: String) -> String {
    return a + b;
}
fn main() { }
)", "@__frappe_str_concat");
}

TEST_CASE("codegen: string equality calls __frappe_str_eq", "[codegen]") {
    CHECK_IR(R"(
fn seq(a: String, b: String) -> Bool {
    return a == b;
}
fn main() { }
)", "@__frappe_str_eq");
}

TEST_CASE("codegen: field read emits getelementptr into struct type",
          "[codegen]") {
    auto r = compile_to_ir(R"(
class P {
    public x: Int;
    fn get() -> Int {
        return this.x;
    }
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "getelementptr inbounds %P"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "load i64"));
}

TEST_CASE("codegen: field write emits getelementptr and store", "[codegen]") {
    auto r = compile_to_ir(R"(
class P {
    public x: Int;
    fn set(v: Int) {
        this.x = v;
    }
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "getelementptr inbounds %P"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "store i64"));
}

TEST_CASE("codegen: array literal calls __frappe_alloc_array", "[codegen]") {
    auto r = compile_to_ir(R"(
fn build(a: Int) -> [Int] {
    return [a];
}
fn main() { let x = build(1); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "@__frappe_alloc_array"));
}

TEST_CASE("codegen: array index emits getelementptr and load", "[codegen]") {
    auto r = compile_to_ir(R"(
fn get(xs: [Int]) -> Int {
    return xs[0];
}
fn main() { let x = get([1]); }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "getelementptr inbounds i64, ptr"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "load i64"));
}

TEST_CASE("codegen: panic calls runtime and emits unreachable", "[codegen]") {
    auto r = compile_to_ir(R"(
fn die() {
    panic("boom");
}
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_contains(*r.ir, "call void @__frappe_panic"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "unreachable"));
}

TEST_CASE("codegen: void function emits ret void", "[codegen]") {
    auto r = compile_to_ir(R"(
fn noop() { }
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_defines_function(*r.ir, "noop"));
    REQUIRE(frappe_test::ir_contains(*r.ir, "ret void"));
}

TEST_CASE("codegen: multiple free functions all get defined", "[codegen]") {
    auto r = compile_to_ir(R"(
fn a() -> Int { return 1; }
fn b() -> Int { return 2; }
fn c() -> Int { return 3; }
fn main() { }
)");
    REQUIRE_FALSE(r.compile->has_errors());
    if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }

    REQUIRE(frappe_test::ir_defines_function(*r.ir, "a"));
    REQUIRE(frappe_test::ir_defines_function(*r.ir, "b"));
    REQUIRE(frappe_test::ir_defines_function(*r.ir, "c"));
    // 3 user functions + __frappe_user_main + main wrapper = 5 defines.
    REQUIRE(frappe_test::ir_count(*r.ir, "define ") >= 5);
}

TEST_CASE("codegen: function with parameters emits typed signature",
          "[codegen]") {
    CHECK_IR(R"(
fn add(a: Int, b: Int) -> Int {
    return a + b;
}
fn main() { }
)", "define i64 @add(i64");
}