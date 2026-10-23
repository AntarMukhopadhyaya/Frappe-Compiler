#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>

#include "../helpers/compile.hpp"

namespace {

    namespace fs = std::filesystem;

    auto compile_example(std::string_view filename) {
        auto path = frappe_test::project_path(
            std::format("examples/{}", filename));

        INFO("Attempting: " << path);

        if (!fs::exists(path)) {
            FAIL("File not found: " << path
                 << "\n  → run 'ls examples/*.ff' to see actual filenames.");
        }

        auto r = frappe_test::compile_file(path);
        if (!r) {
            FAIL("Failed to open: " << path);
        }
        return r;
    }

} // namespace

TEST_CASE("t11_basic compiles cleanly", "[regression]") {
    auto r = compile_example("t11_basic.ff");
    CHECK_NO_ERRORS(r.get());
}

TEST_CASE("t12_errors produces exactly 8 errors", "[regression]") {
    auto r = compile_example("t12_errors.ff");
    REQUIRE(r->error_count() == 8);
}

TEST_CASE("t13_oop produces exactly 5 errors", "[regression]") {
    auto r = compile_example("t13_oop.ff");
    REQUIRE(r->error_count() == 5);
}

TEST_CASE("t14_generics produces 10 instances", "[regression]") {
    auto r = compile_example("t14_generics.ff");
    REQUIRE(r);
    CHECK_NO_ERRORS(r.get());
    REQUIRE(r->instance_count() == 10);
    auto names = r->instance_names();
    REQUIRE(names.size() == 10);
    REQUIRE(names[0] == "duplicate$Int");
    REQUIRE(names[1] == "duplicate$String");
    REQUIRE(names[2] == "first$Int$String");
    REQUIRE(names[3] == "first$String$Int");
    REQUIRE(names[4] == "identity$Int");
    REQUIRE(names[5] == "identity$String");
    REQUIRE(names[6] == "pick$Int");
    REQUIRE(names[7] == "wrap$Float");
    REQUIRE(names[8] == "wrap$Int");
    REQUIRE(names[9] == "wrap_via$Float");
}

TEST_CASE("t15_init_deinit produces exactly 4 errors", "[regression]") {
    auto r = compile_example("t15_init_deinit.ff");
    REQUIRE(r->error_count() == 4);
}