//
// Created by antar on 24/10/26.
//
// Regression guard: fixed-size array literals must have exactly the
// declared number of elements. Before this fix, `[Int; 3] = [1,2,3,4,5]`
// compiled — the literal's element count and the declared size were
// never compared. The MIR builder sizes the runtime allocation from
// the literal's element count, not the declared size, so a mismatch
// would produce an array whose runtime header said N but whose static
// type said M. Bounds checks would fire against the wrong bound.

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"

// ============================================================================
// Correct sizes — should still pass
// ============================================================================

TEST_CASE("array fixed size: exact match passes", "[array][size]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int; 3] = [1, 2, 3];
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("array fixed size: zero-length literal for zero-size array",
          "[array][size]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int; 0] = [];
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("array dynamic size: any element count accepted",
          "[array][size]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int] = [1, 2, 3, 4, 5, 6, 7];
}
)");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("array fixed size: match with mixed element types",
          "[array][size][interface]") {
    auto r = frappe_test::compile(R"(
interface Animal { fn sound() -> Int; }
class Dog implements Animal { fn sound() -> Int { return 1; } }
class Cat implements Animal { fn sound() -> Int { return 2; } }
class Cow implements Animal { fn sound() -> Int { return 3; } }
fn main() {
    let animals: [Animal; 3] = [new Dog(), new Cat(), new Cow()];
}
)");
    CHECK_NO_ERRORS(r.get());
}

// ============================================================================
// Wrong sizes — must be rejected
// ============================================================================

TEST_CASE("array fixed size: too many elements rejected",
          "[array][size][negative][core]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int; 3] = [1, 2, 3, 4, 5];
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("has 5 elements, expected 3") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("array fixed size: too few elements rejected",
          "[array][size][negative][core]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int; 5] = [1, 2, 3];
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("has 3 elements, expected 5") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("array fixed size: empty literal for non-zero size rejected",
          "[array][size][negative]") {
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int; 5] = [];
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("has 0 elements, expected 5") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}

TEST_CASE("array fixed size: single element mismatch uses singular form",
          "[array][size][negative]") {
    // Message grammar: "1 element" not "1 elements".
    auto r = frappe_test::compile(R"(
fn main() {
    let xs: [Int; 3] = [1];
}
)");
    REQUIRE(r->has_errors());
    bool found = false;
    for (const auto &e : r->errors) {
        if (e.message.find("has 1 element, expected 3") != std::string::npos) {
            found = true;
            break;
        }
    }
    CHECK(found);
}