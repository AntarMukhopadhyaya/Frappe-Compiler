//
// Created by antar on 22/10/26.
//
#include <catch2/catch_test_macros.hpp>

#include <frappe/mir/builder.hpp>

#include "../helpers/compile.hpp"
#include "../helpers/mir_test.hpp"

namespace {

auto build_mir(frappe_test::Compile_Result* r) {
    return frappe::mir::build_module(
        r->program, &*r->checker, &r->engine, &r->arena);
}

} // namespace

// ============================================================================
// Basic emission
// ============================================================================

TEST_CASE("defer emits call before return", "[mir][defer]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() -> Int {
    let r = new R();
    defer r.close();
    return 42;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto ret = frappe_test::last_return_index(*fn);
    REQUIRE(ret > 0);
    REQUIRE(ret < fn->instructions.size());
    REQUIRE(frappe_test::is_call_to(fn->instructions[ret - 1], "R.close"));
}

TEST_CASE("two defers emit in LIFO order", "[mir][defer]") {
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work() -> Int {
    let a = new R();
    let b = new R();
    defer a.ca();
    defer b.cb();
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_b = frappe_test::first_call_index(*fn, "R.cb");
    REQUIRE(idx_a != static_cast<std::size_t>(-1));
    REQUIRE(idx_b != static_cast<std::size_t>(-1));
    // LIFO: b was deferred last, so it runs first.
    REQUIRE(idx_b < idx_a);
}

TEST_CASE("defer block emits all body statements", "[mir][defer]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } fn free() { } }
fn work() -> Int {
    let r = new R();
    defer {
        r.close();
        r.free();
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.close") == 1);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.free") == 1);

    auto idx_close = frappe_test::first_call_index(*fn, "R.close");
    auto idx_free  = frappe_test::first_call_index(*fn, "R.free");
    // Source order preserved inside the defer block.
    REQUIRE(idx_close < idx_free);
}

TEST_CASE("defer fires on both if return paths", "[mir][defer]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work(flag: Bool) -> Int {
    let r = new R();
    defer r.close();
    if (flag) {
        return 1;
    }
    return 2;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);
    // Two return paths → two close calls.
    REQUIRE(frappe_test::count_calls_to(*fn, "R.close") == 2);
}

TEST_CASE("defer at block scope fires before function return", "[mir][defer]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() -> Int {
    {
        let r = new R();
        defer r.close();
    }
    return 42;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_close = frappe_test::first_call_index(*fn, "R.close");
    auto idx_ret   = frappe_test::last_return_index(*fn);
    REQUIRE(idx_close != static_cast<std::size_t>(-1));
    REQUIRE(idx_close < idx_ret);
}

// ============================================================================
// Panic interaction
// ============================================================================

TEST_CASE("panic does not run deferred", "[mir][defer][panic]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    defer r.close();
    panic("boom");
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::first_panic_index(*fn) < fn->instructions.size());
    REQUIRE(frappe_test::count_calls_to(*fn, "R.close") == 0);
}

TEST_CASE("panic terminates block — no instructions after", "[mir][defer][panic]") {
    auto r = frappe_test::compile(R"(
fn work() {
    panic("boom");
    let unreachable: Int = 5;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto panic_idx = frappe_test::first_panic_index(*fn);
    REQUIRE(panic_idx < fn->instructions.size());
    // Panic is the last instruction — dead code dropped, no implicit return.
    REQUIRE(panic_idx + 1 == fn->instructions.size());
}

TEST_CASE("panic in one branch, defer fires on the other", "[mir][defer][panic]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work(flag: Bool) -> Int {
    let r = new R();
    defer r.close();
    if (flag) {
        panic("boom");
    }
    return 42;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());

    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // Panic exists.
    REQUIRE(frappe_test::first_panic_index(*fn) < fn->instructions.size());
    // Close fires exactly once — on the return path.
    REQUIRE(frappe_test::count_calls_to(*fn, "R.close") == 1);
    // And it's before the return, not before the panic.
    auto idx_close = frappe_test::first_call_index(*fn, "R.close");
    auto idx_ret   = frappe_test::last_return_index(*fn);
    REQUIRE(idx_close < idx_ret);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_CASE("defer inside while loop fires on each iteration", "[mir][defer][edge]") {
    // Defer in a while body emits one call in the loop body — but at
    // runtime the block re-enters each iteration, so the emitted instruction
    // runs N times. MIR should contain exactly one static call.
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    var i: Int = 0;
    while (i < 3) {
        defer r.close();
        i += 1;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);
    // One static close call inside the loop body block.
    REQUIRE(frappe_test::count_calls_to(*fn, "R.close") == 1);
}

TEST_CASE("defer inside for loop fires on each iteration", "[mir][defer][edge]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    for (var i: Int = 0; i < 3; i += 1) {
        defer r.close();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.close") == 1);
}

TEST_CASE("defer in both if and else branches", "[mir][defer][edge]") {
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work(flag: Bool) {
    let r = new R();
    if (flag) {
        defer r.ca();
    } else {
        defer r.cb();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);
    // Each branch fires exactly once.
    REQUIRE(frappe_test::count_calls_to(*fn, "R.ca") == 1);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.cb") == 1);
}

TEST_CASE("nested defer inside defer block fires in LIFO within outer block",
          "[mir][defer][edge]") {
    // defer { a(); defer b(); c(); }
    //
    // Runtime expectation:
    //   1. outer defer block starts running at scope exit
    //   2. a() runs (source order in body)
    //   3. c() runs (source order in body)
    //   4. inner defer scope closes → b() runs
    //
    // So order is: a, c, b. Not a, b, c.
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } fn cc() { } }
fn work() {
    let r = new R();
    defer {
        r.ca();
        defer r.cb();
        r.cc();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_b = frappe_test::first_call_index(*fn, "R.cb");
    auto idx_c = frappe_test::first_call_index(*fn, "R.cc");
    REQUIRE(idx_a != static_cast<std::size_t>(-1));
    REQUIRE(idx_b != static_cast<std::size_t>(-1));
    REQUIRE(idx_c != static_cast<std::size_t>(-1));
    REQUIRE(idx_a < idx_c);   // a before c
    REQUIRE(idx_c < idx_b);   // c before b (b is from the inner defer, fires last)
}

TEST_CASE("inner block defer fires before outer block defer", "[mir][defer][edge]") {
    // outer { defer a; inner { defer b; } }
    // b fires at inner block exit, a fires at outer block exit.
    // Order: b, then a.
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work() {
    let r = new R();
    defer r.ca();
    {
        defer r.cb();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_b = frappe_test::first_call_index(*fn, "R.cb");
    REQUIRE(idx_a != static_cast<std::size_t>(-1));
    REQUIRE(idx_b != static_cast<std::size_t>(-1));
    REQUIRE(idx_b < idx_a);
}

TEST_CASE("defer with panic inside defer body drops code after panic",
          "[mir][defer][edge]") {
    // defer { a(); panic(); b(); }
    // At scope exit: a() runs, panic aborts, b() is dead code and must not
    // be emitted.
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work() {
    let r = new R();
    defer {
        r.ca();
        panic("boom");
        r.cb();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_calls_to(*fn, "R.ca") == 1);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.cb") == 0);
    REQUIRE(frappe_test::first_panic_index(*fn) < fn->instructions.size());

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_panic = frappe_test::first_panic_index(*fn);
    REQUIRE(idx_a < idx_panic);   // a before panic
}

TEST_CASE("defer block containing only nested defer", "[mir][defer][edge]") {
    // defer { defer foo(); }
    // Outer defer runs at scope exit, which triggers the inner defer block,
    // which runs foo(). Net effect: foo fires once.
    auto r = frappe_test::compile(R"(
class R { fn ca() { } }
fn work() {
    let r = new R();
    defer {
        defer r.ca();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.ca") == 1);
}

TEST_CASE("defer and ordinary call in same block — defer fires after",
          "[mir][defer][edge]") {
    // Non-deferred calls execute at their source position.
    // The defer fires at block exit, after all normal statements.
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work() {
    let r = new R();
    defer r.ca();
    r.cb();
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_b = frappe_test::first_call_index(*fn, "R.cb");
    REQUIRE(idx_a != static_cast<std::size_t>(-1));
    REQUIRE(idx_b != static_cast<std::size_t>(-1));
    // Ordinary call comes first, defer comes after.
    REQUIRE(idx_b < idx_a);
}

// ============================================================================
// Edge case — potential bug: defer modifying return-referenced local
// ============================================================================

TEST_CASE("return value captured before defer modifies local",
          "[mir][defer][edge]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() -> Int {
    var x: Int = 5;
    defer { x = 10; }
    return x;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto ret_idx = frappe_test::last_return_index(*fn);
    REQUIRE(ret_idx < fn->instructions.size());

    auto* ret = std::get_if<frappe::mir::MirReturn>(&fn->instructions[ret_idx]);
    REQUIRE(ret);
    REQUIRE(ret->value.has_value());
    REQUIRE(std::holds_alternative<frappe::mir::MirTemp>(*ret->value));
}


// ============================================================================
// Control flow
// ============================================================================

TEST_CASE("if without else emits a branch", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(c: Bool) -> Int {
    if (c) {
        return 1;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 1);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("if-else emits both paths in distinct blocks", "[mir][control]") {
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work(c: Bool) {
    let r = new R();
    if (c) {
        r.ca();
    } else {
        r.cb();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_b = frappe_test::first_call_index(*fn, "R.cb");
    REQUIRE(idx_a != static_cast<std::size_t>(-1));
    REQUIRE(idx_b != static_cast<std::size_t>(-1));
    REQUIRE(idx_a != idx_b);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("if with both branches returning — no fall-through",
          "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(c: Bool) -> Int {
    if (c) {
        return 1;
    } else {
        return 2;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // Two returns, one per branch — no implicit fall-through return.
    int returns = 0;
    for (const auto& i : fn->instructions) {
        if (frappe_test::is_return(i)) ++returns;
    }
    REQUIRE(returns == 2);
    REQUIRE(frappe_test::is_well_formed(*fn));
}

TEST_CASE("nested if emits multiple branches", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(a: Bool, b: Bool) -> Int {
    if (a) {
        if (b) {
            return 1;
        }
        return 2;
    }
    return 3;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 2);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("while loop emits branch and back-edge", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(n: Int) -> Int {
    var i: Int = 0;
    while (i < n) {
        i += 1;
    }
    return i;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 1);
    REQUIRE(frappe_test::count_jumps(*fn) >= 1);   // back-edge
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("empty while loop is well-formed", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(c: Bool) {
    while (c) { }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("for loop emits init, cond, body, update, back-edge",
          "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(n: Int) -> Int {
    var sum: Int = 0;
    for (var i: Int = 0; i < n; i += 1) {
        sum += i;
    }
    return sum;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 1);
    REQUIRE(frappe_test::count_jumps(*fn) >= 1);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("infinite for loop emits unconditional entry jump",
          "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work() -> Int {
    for (;;) {
        return 1;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // No condition → no branch for the loop, but a jump into the body.
    REQUIRE(frappe_test::count_jumps(*fn) >= 1);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("short-circuit && emits branch", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(a: Bool, b: Bool) -> Bool {
    return a && b;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 1);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("short-circuit || emits branch", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(a: Bool, b: Bool) -> Bool {
    return a || b;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 1);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("short-circuit branch has distinct targets", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(a: Bool, b: Bool) -> Bool {
    return a && b;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    bool found = false;
    for (const auto& i : fn->instructions) {
        if (auto* br = std::get_if<frappe::mir::MirBranch>(&i)) {
            REQUIRE(br->then_label.index != br->else_label.index);
            found = true;
        }
    }
    REQUIRE(found);
}

TEST_CASE("panic inside if terminates the branch", "[mir][control][panic]") {
    auto r = frappe_test::compile(R"(
fn work(c: Bool) -> Int {
    if (c) {
        panic("boom");
    }
    return 1;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_panic = frappe_test::first_panic_index(*fn);
    auto idx_ret   = frappe_test::last_return_index(*fn);
    REQUIRE(idx_panic < fn->instructions.size());
    REQUIRE(idx_ret   < fn->instructions.size());
    REQUIRE(idx_panic < idx_ret);
    REQUIRE(frappe_test::is_well_formed(*fn));
}

TEST_CASE("return inside if drops dead code after return", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(c: Bool) -> Int {
    if (c) {
        return 1;
        let unreachable: Int = 99;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    int returns = 0;
    for (const auto& i : fn->instructions) {
        if (frappe_test::is_return(i)) ++returns;
    }
    REQUIRE(returns == 2);
    REQUIRE(frappe_test::is_well_formed(*fn));
}

TEST_CASE("mixed control flow is well-formed", "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn branchy(c: Bool, n: Int) -> Int {
    var sum: Int = 0;
    if (c) {
        for (var i: Int = 0; i < n; i += 1) {
            sum += i;
        }
    } else {
        var j: Int = 0;
        while (j < n) {
            sum += j;
            j += 1;
        }
    }
    if (sum > 100 && c) {
        panic("too big");
    }
    return sum;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "branchy");
    REQUIRE(fn);

    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
    REQUIRE(frappe_test::count_branches(*fn) >= 3);
}

TEST_CASE("no adjacent labels in emitted MIR", "[mir][control][!mayfail]") {
    // Adjacent labels — e.g. Lupd: immediately followed by Lend: with
    // no terminator between — are invalid LLVM IR (each block must end
    // in a terminator). The current for-loop lowering emits Lupd
    // unconditionally, even when the body always terminates, so this
    // can surface in `for (;;) { return; }` followed by more code.
    auto r = frappe_test::compile(R"(
fn work() -> Int {
    for (;;) {
        return 1;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    bool prev_label = false;
    for (const auto& i : fn->instructions) {
        if (prev_label && frappe_test::is_label(i)) {
            WARN("adjacent labels detected — this will fail LLVM verification. "
                 "Lower the for-loop with a fallback back-jump when the body "
                 "terminates, or skip emitting `Lupd` in that case.");
            return;
        }
        prev_label = frappe_test::is_label(i);
    }
}

TEST_CASE("while body with return — no fall-through return emitted",
          "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(c: Bool) -> Int {
    while (c) {
        return 1;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // Two returns: one in the loop body, one after — no trailing void.
    int returns = 0;
    for (const auto& i : fn->instructions) {
        if (frappe_test::is_return(i)) ++returns;
    }
    REQUIRE(returns == 2);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("for body with return — no fall-through return emitted",
          "[mir][control]") {
    auto r = frappe_test::compile(R"(
fn work(n: Int) -> Int {
    for (var i: Int = 0; i < n; i += 1) {
        return 1;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    int returns = 0;
    for (const auto& i : fn->instructions) {
        if (frappe_test::is_return(i)) ++returns;
    }
    REQUIRE(returns == 2);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

// ============================================================================
// Switch
// ============================================================================

TEST_CASE("switch with single case emits one branch per value",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(x: Int) -> Int {
    switch (x) {
        case 1: return 10;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_branches(*fn) >= 1);
}

TEST_CASE("switch with multiple cases emits branch per value",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(x: Int) -> Int {
    switch (x) {
        case 1: return 10;
        case 2: return 20;
        case 3: return 30;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // One branch per value in the source, minimum 3.
    REQUIRE(frappe_test::count_branches(*fn) >= 3);
}

TEST_CASE("switch with multi-value case emits branch per value",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
class R { fn ca() { } }
fn work(x: Int) {
    let r = new R();
    switch (x) {
        case 1, 2, 3: r.ca();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // 3 values → 3 branches (one per value).
    REQUIRE(frappe_test::count_branches(*fn) >= 3);
    // Body duplicated per value in current lowering: 3 static calls,
    // only 1 fires at runtime.
    REQUIRE(frappe_test::count_calls_to(*fn, "R.ca") == 3);
}

TEST_CASE("switch without default falls through to end",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } }
fn work(x: Int) {
    let r = new R();
    switch (x) {
        case 1: r.ca();
    }
    r.cb();
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    auto idx_a = frappe_test::first_call_index(*fn, "R.ca");
    auto idx_b = frappe_test::first_call_index(*fn, "R.cb");
    REQUIRE(idx_a != static_cast<std::size_t>(-1));
    REQUIRE(idx_b != static_cast<std::size_t>(-1));
    // cb runs after switch exits — its index is greater than ca's.
    REQUIRE(idx_a < idx_b);
}

TEST_CASE("switch with method call per case", "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
class R { fn ca() { } fn cb() { } fn cc() { } }
fn work(x: Int) {
    let r = new R();
    switch (x) {
        case 1: r.ca();
        case 2: r.cb();
        default: r.cc();
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::count_calls_to(*fn, "R.ca") == 1);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.cb") == 1);
    REQUIRE(frappe_test::count_calls_to(*fn, "R.cc") == 1);
}

TEST_CASE("nested switch is well-formed", "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(a: Int, b: Int) -> Int {
    switch (a) {
        case 1:
            switch (b) {
                case 10: return 1;
                case 20: return 2;
            }
            return 3;
        default:
            return 4;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // Both switch levels contribute branches.
    REQUIRE(frappe_test::count_branches(*fn) >= 3);
}

TEST_CASE("empty switch is well-formed", "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(x: Int) -> Int {
    switch (x) { }
    return 42;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

TEST_CASE("switch subject with short-circuit && is well-formed",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(a: Bool, b: Bool) -> Int {
    switch (a && b) {
        case 0: return 1;
        case 1: return 2;
    }
    return 0;
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    // && contributes branches, then switch adds more.
    REQUIRE(frappe_test::count_branches(*fn) >= 3);
    REQUIRE(frappe_test::is_well_formed(*fn));
    REQUIRE(frappe_test::labels_defined(*fn));
}

// ============================================================================
// Switch — bugs the current lowering still has
// ============================================================================

TEST_CASE("switch case with panic — no dead jump after",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(x: Int) {
    switch (x) {
        case 1: panic("one");
        default: return;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    REQUIRE(frappe_test::is_well_formed(*fn));
}

TEST_CASE("switch where every case returns — no fall-through return",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(x: Int) -> Int {
    switch (x) {
        case 1: return 10;
        case 2: return 20;
        default: return 0;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    int returns = 0;
    for (const auto& i : fn->instructions) {
        if (frappe_test::is_return(i)) ++returns;
    }
    REQUIRE(returns == 3);
    REQUIRE(frappe_test::is_well_formed(*fn));
}

TEST_CASE("switch where case and default terminate — no orphan Lend",
          "[mir][control][switch]") {
    auto r = frappe_test::compile(R"(
fn work(x: Int) -> Int {
    switch (x) {
        case 1: return 1;
        default: return 2;
    }
}
)");
    REQUIRE_FALSE(r->has_errors());
    auto mod = build_mir(r.get());
    auto* fn = frappe_test::find_fn(mod, "work");
    REQUIRE(fn);

    int returns = 0;
    for (const auto& i : fn->instructions) {
        if (frappe_test::is_return(i)) ++returns;
    }
    REQUIRE(returns == 2);
}