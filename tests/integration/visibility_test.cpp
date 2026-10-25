//
// Created by antar on 22/10/26.
//
#include <catch2/catch_test_macros.hpp>
#include "../helpers/compile.hpp"

// ============================================================================
// Field access
// ============================================================================

TEST_CASE("private field read from outside errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private x: Int;
}
fn main() {
    let p = new Point();
    let v = p.x;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access private field 'x'");
}

TEST_CASE("private field read from inside class succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private x: Int;
    fn get() -> Int { return this.x; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("private field read from subclass errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private x: Int;
    fn init() { this.x = 0; }
}
class Sub extends Point {
    fn get() -> Int { return this.x; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access private field 'x'");
}

TEST_CASE("protected field read from subclass succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    protected x: Int;
    fn init() { this.x = 0; }
}
class Sub extends Point {
    fn get() -> Int { return this.x; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("protected field read from grandchild succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class A {
    protected x: Int;
    fn init() { this.x = 0; }
}
class B extends A { }
class C extends B {
    fn get() -> Int { return this.x; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("protected field read from outside hierarchy errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    protected x: Int;
    fn init() { this.x = 0; }
}
fn main() {
    let p = new Point();
    let v = p.x;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 8, "cannot access protected field 'x'");
}

// ============================================================================
// Field write
// ============================================================================

TEST_CASE("private field write from outside errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private x: Int;
    fn init() { this.x = 0; }
}
fn main() {
    let p = new Point();
    p.x = 5;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 8, "cannot access private field 'x'");
}

TEST_CASE("private non-final field write from same-class method succeeds",
          "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private x: Int;
    fn init() { this.x = 0; }
    fn set(v: Int) { this.x = v; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("private field write from subclass errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private x: Int;
    fn init() { this.x = 0; }
}
class Sub extends Point {
    fn set(v: Int) { this.x = v; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access private field 'x'");
}

TEST_CASE("protected field write from subclass succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    protected x: Int;
    fn init() { this.x = 0; }
}
class Sub extends Point {
    fn set(v: Int) { this.x = v; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Method calls
// ============================================================================

TEST_CASE("private method call from outside errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private fn secret() -> Int { return 0; }
}
fn main() {
    let p = new Point();
    let v = p.secret();
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access private method 'secret'");
}

TEST_CASE("private method call from same class succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private fn secret() -> Int { return 0; }
    fn pub() -> Int { return this.secret(); }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("protected method call from subclass succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    protected fn helper() -> Int { return 0; }
}
class Sub extends Point {
    fn use() -> Int { return this.helper(); }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("protected method call from outside hierarchy errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    protected fn helper() -> Int { return 0; }
}
fn main() {
    let p = new Point();
    let v = p.helper();
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access protected method 'helper'");
}

TEST_CASE("super.private_method from subclass errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    private fn helper() -> Int { return 0; }
    fn init() { }
}
class Sub extends Point {
    fn init() { let v = super.helper(); }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access private method 'helper'");
}

TEST_CASE("super.protected_method from subclass succeeds", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Point {
    protected fn helper() -> Int { return 0; }
    fn init() { }
}
class Sub extends Point {
    fn init() { let v = super.helper(); }
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Static method visibility
// ============================================================================

TEST_CASE("private static method call from outside errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Math {
    private static fn helper() -> Int { return 0; }
}
fn main() {
    let v = Math.helper();
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 6, "cannot access private static method 'helper'");
}

TEST_CASE("private static method call from inside class succeeds",
          "[visibility]") {
    auto r = frappe_test::compile(R"(
class Math {
    private static fn helper() -> Int { return 0; }
    static fn public_api() -> Int { return Math.helper(); }
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Module-scope access (current_class == nullptr)
// ============================================================================

TEST_CASE("private field at module scope errors", "[visibility]") {
    auto r = frappe_test::compile(R"(
class Config {
    private secret: Int;
    fn init() { this.secret = 42; }
}
let global_cfg = new Config();
let leaked = global_cfg.secret;
)");
    REQUIRE(r->has_errors());
    CHECK_ERROR_LINE(r.get(), 0, 7, "cannot access private field 'secret'");
}

// ============================================================================
// Mixed visibility on same class — only restricted members warn
// ============================================================================

TEST_CASE("mix of public and private members - only private access errors",
          "[visibility]") {
    auto r = frappe_test::compile(R"(
class Foo {
    public a: Int;
    private b: Int;
    fn init() { this.a = 1; this.b = 2; }
}
fn main() {
    let f = new Foo();
    let x = f.a;      // OK — public
    let y = f.b;      // ERROR — private
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 10, "cannot access private field 'b'");
}