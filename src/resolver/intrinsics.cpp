//
// Created by antar on 24/10/26.
//

#include <frappe/resolver/intrinsics.hpp>

namespace frappe::resolver {
    namespace {
        constexpr IntrinsicOverload PRINTLN_OVERLOADS[] = {
            {Builtin::Int, "__frappe_println_int"},
            {Builtin::Float, "__frappe_println_float"},
            {Builtin::String, "__frappe_println_str"},
        };
        constexpr Intrinsic INTRINSICS[] = {
            {"println", 1, PRINTLN_OVERLOADS, std::size(PRINTLN_OVERLOADS)},
        };
    }

    const Intrinsic *find_intrinsic(std::string_view name) noexcept {
        for (const auto& i: INTRINSICS) {
            if (i.name == name) return &i;
        }
        return nullptr;
    }
}
