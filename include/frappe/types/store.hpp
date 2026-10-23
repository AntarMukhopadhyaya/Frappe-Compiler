#pragma once

#include <cstdint>
#include <memory_resource>
#include <span>
#include <unordered_map>

#include <frappe/resolver/builtin.hpp>
#include <frappe/types/types.hpp>

namespace frappe::types {
    struct Store {
        std::pmr::memory_resource *mr = nullptr;

        Type *int_type = nullptr;
        Type *float_type = nullptr;
        Type *bool_type = nullptr;
        Type *string_type = nullptr;
        Type *char_type = nullptr;
        Type *nil_type = nullptr;
        Type *void_type = nullptr;
        Type *int8_type = nullptr;
        Type *int16_type = nullptr;
        Type *int32_type = nullptr;
        Type *int64_type = nullptr;
        Type *uint8_type = nullptr;
        Type *uint16_type = nullptr;
        Type *uint32_type = nullptr;
        Type *uint64_type = nullptr;
        Type *float32_type = nullptr;
        Type *float64_type = nullptr;

        std::pmr::unordered_map<const void *, Type *> named_type_cache;

        Type *unknown_type = nullptr;
        Type *error_type = nullptr;
    };

    [[nodiscard]] Store make_store(std::pmr::memory_resource *mr);

    [[nodiscard]] Type *builtin_type(Store *s, resolver::Builtin b);

    [[nodiscard]] Type *named_type(Store *s, NamedTypeDeclRef ref);

    [[nodiscard]] Type *array_type(Store *s, Type *elem, std::int64_t size);

    [[nodiscard]] Type *optional_type(Store *s, Type *inner);

    [[nodiscard]] Type *func_type(Store *s, std::span<Type * const> params, Type *ret);

    [[nodiscard]] Type *tuple_type(Store *s, std::span<Type * const> elems);


} // namespace frappe::types
