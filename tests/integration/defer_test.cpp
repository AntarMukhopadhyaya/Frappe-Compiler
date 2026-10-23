//
// Created by antar on 22/10/26.
//
#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

// ============================================================================
// Parsing
// ============================================================================

TEST_CASE("defer expression form parses", "[defer][parser]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    defer r.close();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("defer block form parses", "[defer][parser]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    defer {
        r.close();
    }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("defer block with multiple statements", "[defer][parser]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } fn free() { } }
fn work() {
    let r = new R();
    defer {
        r.close();
        r.free();
    }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("defer block form takes no trailing semicolon", "[defer][parser]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    defer { r.close(); }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("defer expression form requires semicolon", "[defer][parser]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() {
    let r = new R();
    defer r.close()
}
)");
    REQUIRE(r->has_errors());
}

// ============================================================================
// `return` inside defer is rejected
// ============================================================================

TEST_CASE("bare return inside defer errors", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
fn work() {
    defer {
        return;
    }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'return' not allowed inside 'defer'");
}

TEST_CASE("return inside if inside defer errors", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
fn work(flag: Bool) {
    defer {
        if (flag) {
            return;
        }
    }
}
)");
    REQUIRE(r->has_errors());
    CHECK_ERROR_LINE(r.get(), 0, 3, "'return' not allowed inside 'defer'");
}

TEST_CASE("return inside for inside defer errors", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
fn work() {
    defer {
        for (let i: Int = 0; i < 10; i += 1) {
            return;
        }
    }
}
)");
    REQUIRE(r->has_errors());
    CHECK_ERROR_LINE(r.get(), 0, 3, "'return' not allowed inside 'defer'");
}

TEST_CASE("return inside switch inside defer errors", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
fn work() {
    defer {
        switch (1) {
            case 1:
                return;
            default:
        }
    }
}
)");
    REQUIRE(r->has_errors());
    CHECK_ERROR_LINE(r.get(), 0, 3, "'return' not allowed inside 'defer'");
}

TEST_CASE("nested defer with return errors", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
fn work() {
    defer {
        defer {
            return;
        }
    }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 4, "'return' not allowed inside 'defer'");
}

// ============================================================================
// Valid uses
// ============================================================================

TEST_CASE("defer with method call in valid context", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() -> Int {
    let r = new R();
    defer r.close();
    return 42;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("multiple defers in same block", "[defer][checker]") {
    auto r = frappe_test::compile(R"(
class R { fn close() { } }
fn work() -> Int {
    let a = new R();
    let b = new R();
    defer a.close();
    defer b.close();
    return 0;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("defer block with multiple statements in valid context", "[defer][checker]") {
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
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("defer at block scope is valid", "[defer][checker]") {
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
    CHECK_NO_ERRORS(r.get());
}