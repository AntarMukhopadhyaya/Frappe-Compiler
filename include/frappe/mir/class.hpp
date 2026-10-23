//
// Created by antar on 21/10/26.
//

#pragma once

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string_view>
#include <vector>


#include <frappe/types/types.hpp>

namespace frappe::mir {
    // Field layout - computed by MIR builder, consumed by codegen
    struct MirField {
        std::string_view name;
        const types::Type *type;
        std::uint32_t index; // position in class (0-based)
    };

    struct MirMethod {
        std::string_view name;
        std::string_view fn_name;
        std::optional<std::uint32_t> vtable_slot = std::nullopt;
    };

    // Class metadata — one entry per class in MirModule::classes.
    //
    // Fields start at offset 0 for root classes, after the base class's fields
    // for derived classes. If v2 adds virtual dispatch, `has_vtable` gates whether
    // field offsets start at 0 or at sizeof(vptr). Reserved now so the offset
    // computation stays in one place forever.

    struct MirClass {
        std::string_view name;
        std::optional<std::string_view> base_name; // for inheritance
        std::pmr::vector<MirField> fields;
        std::pmr::vector<MirMethod> methods;
        bool has_init;
        bool has_deinit;
        std::pmr::vector<std::string_view> vtable_slots;
        bool has_vtable;
        bool is_final;
        bool is_abstract;
    };
}
