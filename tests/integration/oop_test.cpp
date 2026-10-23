//
// Created by antar on 21/10/26.
//


#include <ranges>
#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

TEST_CASE("minimal class compiles cleanly", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
    y: Float;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("class with this.x works", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
    fn get() -> Float { return this.x; }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("inheritance resolves base class", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
    fn sum() -> Float { return this.x; }
}
class Circle extends Point {
    radius: Float;
}
fn main() {
    let c = new Circle();
    let s = c.sum();      // inherited
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("this outside a method errors", "[oop]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let x = this;
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'this' is only valid inside a method");
}

TEST_CASE("super without base class errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    fn get() -> Float { return super.x; }
}
)");
    // Resolver error for 'super' first
    REQUIRE(r->error_count() >= 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'super'");
}

TEST_CASE("unknown member errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
}
fn main() {
    let p = new Point();
    let y = p.nonexistent;    // ← actual line 7, not 6
}
)");
    REQUIRE(r->error_count() >= 1);
    CHECK_ERROR_LINE(r.get(), 0, 7, "no member 'nonexistent'");   // was 6
}

TEST_CASE("init cannot have a return type", "[oop]") {
    auto r = frappe_test::compile(R"(
class Bad {
    fn init(x: Float) -> Int { return 0; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'init' cannot declare a return type");
}

TEST_CASE("init cannot be static", "[oop]") {
    auto r = frappe_test::compile(R"(
class Bad {
    static fn init(x: Float) {}
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'init' cannot be static");
}

TEST_CASE("deinit must take exactly one parameter", "[oop]") {
    auto r = frappe_test::compile(R"(
class Bad {
    fn deinit() {}
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'deinit' must take exactly one parameter");
}

TEST_CASE("deinit cannot have a return type", "[oop]") {
    auto r = frappe_test::compile(R"(
class Bad {
    fn deinit(alloc: Int) -> Int { return 0; }
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 3, "'deinit' cannot declare a return type");
}

TEST_CASE(".init() call is rejected", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
    fn init(x: Float) { this.x = x; }
}
fn main() {
    let p = new Point(1.0);
    p.init(2.0);              // ← actual line 8, not 7
}
)");
    REQUIRE(r->error_count() == 1);
    CHECK_ERROR_LINE(r.get(), 0, 8, "use 'new Class(...)'");      // was 7
}


TEST_CASE("super.init(x) is allowed", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
    fn init(x: Float) { this.x = x; }
}
class Circle extends Point {
    radius: Float;
    fn init(x: Float, r: Float) {
        super.init(x);
        this.radius = r;
    }
}
)");
    CHECK_NO_ERRORS(r.get());
}
TEST_CASE("inherited method records the declaring class name", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
    fn sum() -> Float { return this.x; }
}
class Circle extends Point {
    radius: Float;
}
fn main() {
    let c = new Circle();
    let s = c.sum();
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->checker->call_names.size() == 1);
    for (const auto& [node, name] : r->checker->call_names) {
        REQUIRE(name == "Point.sum");
    }
}

TEST_CASE("multi-level inheritance resolves inherited methods", "[oop]") {
    auto r = frappe_test::compile(R"(
class Animal {
    fn speak() -> Int { return 1; }
}
class Mammal extends Animal {
}
class Dog extends Mammal {
}
fn main() {
    let d = new Dog();
    let result = d.speak();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("inherited field access resolves", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Float;
}
class ColoredPoint extends Point {
    color: Int;
}
fn main() {
    let p = new ColoredPoint();
    let x = p.x;
    let c = p.color;
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("derived class can access inherited field through this", "[oop]") {
    auto r = frappe_test::compile(R"(
class Base {
    x: Float;
}
class Derived extends Base {
    fn get() -> Float {
        return this.x;
    }
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("unknown base class errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class Child extends Missing {
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("cyclic inheritance errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class A extends B {
}
class B extends A {
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("longer cyclic inheritance errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class A extends B {
}
class B extends C {
}
class C extends A {
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("base declared after derived class resolves", "[oop]") {
    auto r = frappe_test::compile(R"(
class Derived extends Base {
    fn get() -> Int { return this.x; }
}
class Base {
    x: Int;
}
fn main() {
    let d = new Derived();
    let value = d.get();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("unrelated classes can declare methods with the same name", "[oop]") {
    auto r = frappe_test::compile(R"(
class A {
    fn value() -> Int { return 1; }
}
class B {
    fn value() -> Int { return 2; }
}
fn main() {
    let a = new A();
    let b = new B();
    let x = a.value();
    let y = b.value();
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("inherited method records original declaring class", "[oop]") {
    auto r = frappe_test::compile(R"(
class A {
    fn value() -> Int { return 1; }
}
class B extends A {
}
class C extends B {
}
fn main() {
    let c = new C();
    let x = c.value();
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->checker->call_names.size() == 1);
    for (const auto& [node, name] : r->checker->call_names) {
        REQUIRE(name == "A.value");
    }
}

TEST_CASE("field accessed as method errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Int;
}
fn main() {
    let p = new Point();
    p.x();
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("method accessed as field is diagnosed appropriately", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    fn get() -> Int { return 1; }
}
fn main() {
    let p = new Point();
    let x = p.get;
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("super.init without a base class errors", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    fn init() {
        super.init();
    }
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("constructor arguments are type checked", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Int;
    fn init(x: Int) {
        this.x = x;
    }
}
fn main() {
    let p = new Point("wrong");
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("constructor argument count is checked", "[oop]") {
    auto r = frappe_test::compile(R"(
class Point {
    x: Int;
    fn init(x: Int) {
        this.x = x;
    }
}
fn main() {
    let p = new Point();
}
)");
    REQUIRE(r->error_count() >= 1);
}

TEST_CASE("inherited method call records declaring class not receiver class", "[oop]") {
    auto r = frappe_test::compile(R"(
class Base {
    fn compute() -> Int { return 42; }
}
class Derived extends Base {
}
fn main() {
    let d = new Derived();
    let result = d.compute();
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->checker->call_names.size() == 1);
    for (const auto &name: r->checker->call_names | std::views::values) {
        REQUIRE(name == "Base.compute");
    }
}