//
// Created by antar on 19/10/26.
//

#pragma once


#include <cstdint>
#include <format>
#include <iterator>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <frappe/source/span.hpp>
#include <frappe/diagnostics/diagnostic.hpp>

namespace frappe::diagnostics {
    struct Engine {
        std::pmr::memory_resource *mr;
        std::pmr::vector<Diagnostic> diagnostics;
        std::uint32_t error_count = 0;
    };

    [[nodiscard]] inline Engine make_engine(std::pmr::memory_resource *mr) {
        return Engine{
            .mr = mr,
            .diagnostics = std::pmr::vector<Diagnostic>{mr},
            .error_count = 0
        };
    }

    [[nodiscard]] inline bool has_errors(const Engine *e) noexcept {
        return e->error_count > 0;
    }

    void error(Engine *e, const source::Span &span, std::string_view msg);

    void warning(Engine *e, const source::Span &span, std::string_view msg);

    void info(Engine *e, const source::Span &span, std::string_view msg);

    void print(const Engine *e);

    [[nodiscard]] constexpr std::string_view level_name(DiagnosticLevel level) noexcept {
        switch (level) {
            case DiagnosticLevel::Error: return "error";
            case DiagnosticLevel::Warning: return "warning";
            case DiagnosticLevel::Info: return "info";
        }
        return "unknown";
    }

    template<typename... Args>
    void report_error(Engine *e, source::Span span,
                      std::format_string<Args...> fmt, Args &&... args) {
        std::pmr::string msg(e->mr);
        std::format_to(std::back_inserter(msg), fmt, std::forward<Args>(args)...);
        error(e, span, msg);
    }

    namespace detail {
        void push(Engine *e, DiagnosticLevel level, source::Span span,
                  std::pmr::string &&msg);
    }
}
