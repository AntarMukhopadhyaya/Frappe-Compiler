#include <frappe/fatal/fatality.hpp>

#include <cstdio>
#include <cstdlib>

#ifndef FRAPPE_VERSION
#define FRAPPE_VERSION "dev"
#endif

namespace frappe::fatal {

    [[noreturn]] void fatality(const source::Span& span, std::string_view detail) {
        std::fprintf(stderr,
            "\n"
            "╔══════════════════════════════════════════════════════════════════╗\n"
            "║  FATALITY                                                       ║\n"
            "║  Compiler internal error — this is a bug in Frappe,             ║\n"
            "║  not in your program.                                           ║\n"
            "╚══════════════════════════════════════════════════════════════════╝\n"
            "\n"
            "  Frappe %s\n"
            "  at [%u:%u]\n"
            "\n"
            "  %.*s\n"
            "\n"
            "  Please report at: https://github.com/antar/frappe/issues\n"
            "\n",
            FRAPPE_VERSION,
            static_cast<unsigned>(span.start.line),
            static_cast<unsigned>(span.start.column),
            static_cast<int>(detail.size()), detail.data());

        std::fflush(stderr);

#if !defined(NDEBUG)
        std::abort();
#else
        std::exit(101);
#endif
    }

} // namespace frappe::fatal