//
// Created by antar on 19/10/26.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace frappe::resolver {
    enum class Builtin : std::uint8_t {
        Int, Float, String, Char, Bool, Nil, Void,
        Int8, Int16, Int32, Int64,
        UInt8, UInt16, UInt32, UInt64,
        Float32, Float64,
    };

    inline constexpr std::array<std::string_view, 17> BUILTIN_NAMES = {
        "Int", "Float", "String", "Char", "Bool", "Nil", "Void",
        "Int8", "Int16", "Int32", "Int64",
        "UInt8", "UInt16", "UInt32", "UInt64",
        "Float32", "Float64",
    };
    [[nodiscard]] constexpr std::optional<Builtin>
    builtin_from_name(std::string_view name) noexcept {
        for (std::size_t i = 0; i < BUILTIN_NAMES.size(); ++i) {
            if (BUILTIN_NAMES[i] == name) return static_cast<Builtin>(i);
        }
        return std::nullopt;
    }
}
