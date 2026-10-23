//
// Created by antar on 19/10/26.
//

#pragma  once

#include <memory_resource>
#include <cstdint>

#include <string>
#include <frappe/source/span.hpp>

namespace frappe::diagnostics {
    enum class DiagnosticLevel : std::uint8_t {
        Info,
        Warning,
        Error
    };

    struct Diagnostic {
        DiagnosticLevel level;
        std::pmr::string message;
        source::Span span;
    };
}
