//
// Created by antar on 21/10/26.
//

#pragma once

#include <string_view>
#include <frappe/mir/value.hpp>
#include <frappe/types/types.hpp>

namespace  frappe::mir {
    struct MirLocal {
        MirLocalId id;
        std::string_view name;
        const types::Type* type;
    };
}