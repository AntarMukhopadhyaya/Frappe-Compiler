//
// Created by antar on 19/10/26.
//
#include <frappe/diagnostics/engine.hpp>

#include <cstdio>

namespace frappe::diagnostics {
    namespace detail {
        void push(Engine *e, DiagnosticLevel level, source::Span span, std::pmr::string &&msg) {
            e->diagnostics.emplace_back(level, std::move(msg), span);
            if (level == DiagnosticLevel::Error) {
                e->error_count++;
            }
        }
    }

    void error(Engine *e, const source::Span &span, std::string_view msg) {
        detail::push(e, DiagnosticLevel::Error, span, std::pmr::string{msg, e->mr});
    }

    void warning(Engine *e, const source::Span &span, std::string_view msg) {
        detail::push(e, DiagnosticLevel::Warning, span, std::pmr::string{msg, e->mr});
    }

    void info(Engine *e, const source::Span &span, std::string_view msg) {
        detail::push(e, DiagnosticLevel::Info, span, std::pmr::string{msg, e->mr});
    }

    void print(const Engine *e) {
        for (const auto &d: e->diagnostics) {
            std::fprintf(stderr, "[%u:%u] %.*s: %.*s\n",
                         static_cast<unsigned>(d.span.start.line),
                         static_cast<unsigned>(d.span.start.column),
                         static_cast<int>(level_name(d.level).size()),
                         level_name(d.level).data(),
                         static_cast<int>(d.message.size()),
                         d.message.data());
        }
        std::fflush(stderr);
    }
}
