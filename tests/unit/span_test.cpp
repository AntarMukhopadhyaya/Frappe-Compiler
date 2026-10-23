//
// Created by antar on 21/10/26.
//


#include <catch2/catch_test_macros.hpp>

#include <frappe/source/span.hpp>

using namespace frappe::source;

TEST_CASE("join combines start and end", "[source]") {
    Span a{{0, 1, 1}, {5, 1, 6}};
    Span b{{5, 1, 6}, {10, 1, 11}};
    auto c = join(a, b);
    REQUIRE(c.start.offset == 0);
    REQUIRE(c.end.offset == 10);
}

TEST_CASE("join on disjoint spans still uses b's end", "[source]") {
    Span a{{0, 1, 1}, {5, 1, 6}};
    Span b{{20, 2, 1}, {25, 2, 6}};
    auto c = join(a, b);
    REQUIRE(c.start.line == 1);
    REQUIRE(c.end.line   == 2);
}

TEST_CASE("default Location is zeroed except line and column", "[source]") {
    Location loc;
    REQUIRE(loc.offset == 0);
    REQUIRE(loc.line == 1);
    REQUIRE(loc.column == 1);
}