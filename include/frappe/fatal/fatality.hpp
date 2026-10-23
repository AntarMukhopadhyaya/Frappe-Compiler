//
// Created by antar on 21/10/26.
//

#pragma once

#include <cassert>
#include <format>
#include <iterator>
#include <memory_resource>
#include <string_view>
#include <utility>

#include <frappe/source/span.hpp>

namespace frappe::fatal {
    // ============================================================================
    // Fatality — compiler internal error.
    //
    // Reports the invariant that was violated and aborts. Does NOT return: once
    // an invariant is broken, downstream state is untrustworthy and continuing
    // risks silent corruption.
    //
    // This is NOT a diagnostic. Diagnostics report user errors and continue
    // collecting; Fatalities report compiler bugs and halt immediately.
    //
    //   debug   → SIGABRT (core dump for gdb)
    //   release → std::exit(101)   (distinct from exit 1 = user error)
    //
    // Depends only on source::Span — no Engine, no Diagnostic, no format types
    // leaking from diagnostics.
    // ============================================================================

    [[noreturn]] void fatality(const source::Span &span, std::string_view detail);

    template<typename... Args>
    [[noreturn]] inline void fatality_at(
        const source::Span &span,
        std::format_string<Args...> fmt, Args &&... args) {
        // Format into a stack buffer first; fatality may need to be callable
        // when memory resources are in an unknown state.
        char buf[1024];
        auto result = std::format_to_n(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
        std::string_view detail{buf, static_cast<std::size_t>(result.size)};
        fatality(span, detail);
    }
}

// Any compiler-bug abort. Use when you'd otherwise write:
//   assert(false && "..."); std::unreachable();
#define FRAPPE_FATAL(span, ...) \
    ::frappe::fatal::fatality_at(span, __VA_ARGS__)

// Specifically for "this branch should be unreachable."
// Supports both plain and formatted reasons:
//   FRAPPE_UNREACHABLE(span, "reason")
//   FRAPPE_UNREACHABLE(span, "reason with {}", x)
#define FRAPPE_UNREACHABLE(span, msg, ...) \
    FRAPPE_FATAL(span, "unreachable: " msg __VA_OPT__(,) __VA_ARGS__)

// Dev-only precondition — disappears under NDEBUG. Only for pure null-guards
// where the alternative is an immediate null-deref anyway.
#define FRAPPE_ASSERT(cond, reason) \
    do { \
        assert((cond) && reason); \
        if (!(cond)) std::unreachable(); \
    } while (0)
