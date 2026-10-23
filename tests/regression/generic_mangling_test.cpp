//
// Created by antar on 23/10/26.
//

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

// Regression guard for the write_type_name collision: distinct structural
// types must produce distinct mangled names. Prior to the fix, every function
// type was mangled as `<func>` and every tuple as `<tuple>`, so two generic
// instantiations with different function/tuple args collided in the cache
// and the second call got checked against the first call's param types.

TEST_CASE("generic mangling: distinct function-type args produce distinct instances",
          "[generic][mangling][regression]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn takes_int(x: Int) -> Int { return x; }
fn takes_str(x: String) -> String { return x; }
fn main() {
    identity(takes_int);
    identity(takes_str);
}
)");
    REQUIRE_FALSE(r->has_errors());

    auto names = r->instance_names();
    int identity_count = 0;
    for (const auto& n : names) {
        if (n.starts_with("identity$")) ++identity_count;
    }
    // Two structurally different instantiations must exist.
    CHECK(identity_count == 2);
}

TEST_CASE("generic mangling: function-type instances embed signature",
          "[generic][mangling]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn takes_int(x: Int) -> Int { return x; }
fn main() {
    identity(takes_int);
}
)");
    REQUIRE_FALSE(r->has_errors());

    // The mangled name must include the signature, not a placeholder.
    auto names = r->instance_names();
    bool found = false;
    for (const auto& n : names) {
        if (n.find("(Int) -> Int") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("generic mangling: distinct tuple-type args produce distinct instances",
          "[generic][mangling][regression]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn takes_pair(p: (Int, String)) -> Int {
    identity(p);
    return 0;
}
fn takes_single(p: (Int)) -> Int {
    identity(p);
    return 0;
}
fn main() { }
)");
    REQUIRE_FALSE(r->has_errors());

    auto names = r->instance_names();
    int identity_count = 0;
    for (const auto& n : names) {
        if (n.starts_with("identity$")) ++identity_count;
    }
    // Two structurally different tuple instantiations.
    CHECK(identity_count == 2);
}

TEST_CASE("generic mangling: builtin instantiations unchanged",
          "[generic][mangling]") {
    // Sanity: existing manglings for builtin type args don't change.
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() {
    identity(1);
    identity("hi");
}
)");
    REQUIRE_FALSE(r->has_errors());

    auto names = r->instance_names();
    bool has_int = false, has_string = false;
    for (const auto& n : names) {
        if (n == "identity$Int") has_int = true;
        if (n == "identity$String") has_string = true;
    }
    CHECK(has_int);
    CHECK(has_string);
}