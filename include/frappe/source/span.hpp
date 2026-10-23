//
// Created by antar on 19/10/26.
//

#pragma  once
#include <cstddef>
#include <cstdint>

namespace  frappe::source {
    struct Location {
        std::size_t offset = 0;
        std::uint32_t line = 1;
        std::uint32_t column = 1;
    };
    struct Span {
        Location start;
        Location end;
    };
    [[nodiscard]] inline Span join(Span a, Span b) noexcept {
        return Span{.start = a.start, .end = b.end};
    }
}