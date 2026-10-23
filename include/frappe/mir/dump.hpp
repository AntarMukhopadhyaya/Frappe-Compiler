//
// Created by antar on 21/10/26.
//

#pragma once

#include <frappe/mir/module.hpp>


namespace frappe::mir {

    // Renders the entire MIR module as human-readable text.
    //
    // Format guarantees (used by tests — do not change without updating tests):
    //   - One instruction per line, prefixed by `N: ` where N is the
    //     instruction's index in `MirFunction::instructions`.
    //   - Temp writes show `%N:Type` so codegen decisions are inspectable.
    //   - Labels appear on their own line as `N: L<k>:`.
    //   - Classes precede functions; classes in declaration order, functions
    //     in lowering order (methods first, then free fns, then generics).
    //   - Blank line between top-level entities.
    [[nodiscard]] std::pmr::string dump_module(
        const MirModule& module,
        std::pmr::memory_resource* mr);

} 