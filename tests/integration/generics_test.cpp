//
// Created by antar on 21/10/26.
//
#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

TEST_CASE("generic identity infers Int", "[generics]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() { let a: Int = identity(1); }
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 1);
    REQUIRE(r->instance_names()[0] == "identity$Int");
}

TEST_CASE("generic identity infers String", "[generics]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() { let b: String = identity("hi"); }
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 1);
    REQUIRE(r->instance_names()[0] == "identity$String");
}

TEST_CASE("same type args share one instance", "[generics]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() {
    let a: Int = identity(1);
    let b: Int = identity(2);
    let c: Int = identity(3);
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 1);   // dedup working
}

TEST_CASE("two distinct type args create two instances", "[generics]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() {
    let a = identity(1);
    let b = identity("hi");
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 2);
    auto names = r->instance_names();
    REQUIRE(names[0] == "identity$Int");
    REQUIRE(names[1] == "identity$String");
}

TEST_CASE("nested generic call shares inner instance", "[generics]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() {
    let a = identity(identity(5));
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 1);   // still one identity$Int
}

TEST_CASE("multi type param inference", "[generics]") {
    auto r = frappe_test::compile(R"(
fn first<A, B>(a: A, b: B) -> A { return a; }
fn main() {
    let x: Int    = first(1, "hi");
    let y: String = first("a", 2);
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 2);
    auto names = r->instance_names();
    REQUIRE(names[0] == "first$Int$String");
    REQUIRE(names[1] == "first$String$Int");
}

TEST_CASE("cannot infer unbound type parameter", "[generics]") {
    auto r = frappe_test::compile(R"(
fn no_arg<T>() -> T { }
fn main() {
    let x = no_arg();
}
)");
    REQUIRE(r->error_count() >= 1);
    bool found_infer_error = false;
    for (const auto& e : r->errors) {
        if (e.message.find("cannot infer") != std::string::npos) {
            found_infer_error = true;
            break;
        }
    }
    REQUIRE(found_infer_error);
}

TEST_CASE("wrap infers array element", "[generics]") {
    auto r = frappe_test::compile(R"(
fn wrap<T>(x: T) -> [T] {
    let arr: [T] = [x];
    return arr;
}
fn main() {
    let a: [Int] = wrap(42);
}
)");
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 1);
    REQUIRE(r->instance_names()[0] == "wrap$Int");
}

TEST_CASE("generic instances have separate node_types", "[generics]") {
    auto r = frappe_test::compile(R"(
fn twice<T>(x: T) -> T { return x + x; }
fn main() {
    let a = twice(1);
    let b = twice("hi");
}
)");
    REQUIRE(r->instance_count() == 2);
    // Both instances exist with their own node_types map — checked later by MIR
}

TEST_CASE("transitive generic call creates novel instance", "[generics]") {
    auto r = frappe_test::compile(R"(
fn wrap<T>(x: T) -> [T] {
    let arr: [T] = [x];
    return arr;
}
fn wrap_via<T>(x: T) -> [T] {
    return wrap(x);
}
fn main() {
    let wid: [Float] = wrap_via(1.5);
}
)");
    CHECK_NO_ERRORS(r.get());

    // Two instances: wrap_via$Float is called directly from main,
    // wrap$Float is only reachable transitively — main never calls wrap()
    // with a Float directly.
    REQUIRE(r->instance_count() == 2);
    auto names = r->instance_names();
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "wrap$Float");
    REQUIRE(names[1] == "wrap_via$Float");
}

TEST_CASE("transitive generic call reuses cached instance", "[generics]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn duplicate<T>(x: T) -> T { return identity(x); }
fn main() {
    let a = identity(1);
    let b = duplicate(1);
}
)");
    CHECK_NO_ERRORS(r.get());

    // Three calls, two instances:
    //   identity$Int from `identity(1)` in main, reused by duplicate$Int's body
    //   duplicate$Int from `duplicate(1)` in main
    // Not 3 — the transitive identity call hits the cache.
    REQUIRE(r->instance_count() == 2);
    auto names = r->instance_names();
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "duplicate$Int");
    REQUIRE(names[1] == "identity$Int");
}