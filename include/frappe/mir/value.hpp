//
// Created by antar on 21/10/26.
//

#pragma once

#include <compare>
#include <cstdint>
#include <string_view>
#include <variant>


namespace frappe::mir {
    struct MirTemp {
        std::uint32_t index;

        auto operator<=>(const MirTemp &) const = default;
    };

    struct MirLocalId {
        std::uint32_t index;

        auto operator<=>(const MirLocalId &) const = default;
    };

    struct MirLabel {
        std::uint32_t index;

        auto operator<=>(const MirLabel &) const = default;
    };

    struct MirConstantId {
        std::uint32_t index;

        auto operator<=>(const MirConstantId &) const = default;
    };

    struct MirNil {
    };

    using MirConstant = std::variant<std::int64_t, double, bool, std::string_view, MirNil>;

    using MirValue = std::variant<MirTemp, MirLocalId, MirConstantId>;

    [[nodiscard]] inline bool is_temp(const MirValue &v) noexcept {
        return std::holds_alternative<MirTemp>(v);
    }


    [[nodiscard]] inline bool is_local(const MirValue &v) noexcept { return std::holds_alternative<MirLocalId>(v); }

    [[nodiscard]] inline bool is_constant(const MirValue &v) noexcept {
        return std::holds_alternative<MirConstantId>(v);
    }
}
