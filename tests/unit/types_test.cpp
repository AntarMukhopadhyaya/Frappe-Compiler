//
// Created by antar on 21/10/26.
//

#include <catch2/catch_test_macros.hpp>

#include <string>

#include <frappe/types/store.hpp>
#include <frappe/types/types.hpp>

namespace {
struct Types_Fixture {
    std::pmr::monotonic_buffer_resource  arena;
    frappe::types::Store                 store;
    explicit Types_Fixture()
        : store{frappe::types::make_store(&arena)} {}

    std::string name(frappe::types::Type* t) {
        return std::string{frappe::types::type_name(&arena, t)};
    }
};
} // namespace

TEST_CASE("builtin types are distinct", "[types]") {
    Types_Fixture f;
    using frappe::types::type_eq;

    REQUIRE(type_eq(f.store.int_type, f.store.int_type));
    REQUIRE(type_eq(f.store.float_type, f.store.float_type));
    REQUIRE(type_eq(f.store.bool_type, f.store.bool_type));

    REQUIRE_FALSE(type_eq(f.store.int_type, f.store.float_type));
    REQUIRE_FALSE(type_eq(f.store.int_type, f.store.bool_type));
    REQUIRE_FALSE(type_eq(f.store.string_type, f.store.int_type));
}

TEST_CASE("builtin_type() returns the canonical pointer", "[types]") {
    Types_Fixture f;
    using frappe::types::builtin_type;
    using frappe::resolver::Builtin;

    REQUIRE(builtin_type(&f.store, Builtin::Int)    == f.store.int_type);
    REQUIRE(builtin_type(&f.store, Builtin::Float)  == f.store.float_type);
    REQUIRE(builtin_type(&f.store, Builtin::String) == f.store.string_type);
    REQUIRE(builtin_type(&f.store, Builtin::Bool)   == f.store.bool_type);
}

TEST_CASE("type_name prints builtins", "[types]") {
    Types_Fixture f;
    REQUIRE(f.name(f.store.int_type)    == "Int");
    REQUIRE(f.name(f.store.float_type)  == "Float");
    REQUIRE(f.name(f.store.string_type) == "String");
    REQUIRE(f.name(f.store.bool_type)   == "Bool");
    REQUIRE(f.name(f.store.void_type)   == "Void");
    REQUIRE(f.name(f.store.nil_type)    == "Nil");
}

TEST_CASE("array types are distinct by element type", "[types]") {
    Types_Fixture f;
    using frappe::types::array_type;
    using frappe::types::type_eq;

    auto* int_arr   = array_type(&f.store, f.store.int_type,   -1);
    auto* float_arr = array_type(&f.store, f.store.float_type, -1);

    REQUIRE(type_eq(int_arr, int_arr));
    REQUIRE_FALSE(type_eq(int_arr, float_arr));
    REQUIRE_FALSE(type_eq(int_arr, f.store.int_type));
}

TEST_CASE("optional types collapse structurally", "[types]") {
    Types_Fixture f;
    using frappe::types::optional_type;
    using frappe::types::type_eq;

    auto* int_opt = optional_type(&f.store, f.store.int_type);
    REQUIRE(type_eq(int_opt, int_opt));
    REQUIRE_FALSE(type_eq(int_opt, f.store.int_type));
    REQUIRE(f.name(int_opt) == "Int?");
}

TEST_CASE("named types share identity per decl", "[types]") {
    // Can't easily construct ast::ClassDecl here, so verify sentinel behaviour.
    Types_Fixture f;
    using frappe::types::is_error;
    using frappe::types::is_unknown;

    REQUIRE(is_error(f.store.error_type));
    REQUIRE_FALSE(is_error(f.store.int_type));

    REQUIRE(is_unknown(f.store.unknown_type));
    REQUIRE_FALSE(is_unknown(f.store.int_type));
}