//
// Created by antar on 24/10/26.
//

#include <catch2/catch_test_macros.hpp>

#include "../helpers/compile.hpp"


TEST_CASE("REPRO: two instances with different types", "[cache-repro]") {
    auto r = frappe_test::compile(R"(
fn identity<T>(x: T) -> T { return x; }
fn main() {
    let a = identity(1);
    let b = identity("hi");
}
)");
    INFO("error count = " << r->error_count());
    for (const auto &e : r->errors) {
        INFO("[" << e.line << ":" << e.column << "] " << e.message);
    }
    CHECK_NO_ERRORS(r.get());
}