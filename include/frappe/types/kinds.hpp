//
// Created by antar on 19/10/26.
//

#pragma once

#include <cstdint>

namespace frappe::types {
    enum class TypeKind : std::uint8_t {
        Builtin, Named, Array, Optional, Func, Tuple, TypeVar, Unknown, Error,
    };
}