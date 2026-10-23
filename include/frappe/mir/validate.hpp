//
// Created by antar on 23/10/26.
//
// Structural validation for MirModule. Runs after build_module completes,
// before any consumer (codegen, dumps) touches the MIR. Guarantees that:
//
//   * Every MirTemp / MirLocalId / MirConstantId is in bounds.
//   * Every jump/branch target is a defined label in the same function.
//   * Every function ends with a terminator; no dead code after one.
//
// The invariant this enforces — "any MirModule from build_module is
// well-formed" — lets codegen index MIR vectors without bounds checks.
// MIR bugs (builder regressions, generic-instance mistakes) surface as
// diagnostics instead of as UB / crashes.
//
#pragma once
#include <cstdint>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>


namespace frappe::diagnostics {
    struct Engine;
}

namespace frappe::mir {
    struct MirModule;

    struct ValidationError {
        std::pmr::string function_name;
        std::pmr::string message;
    };

    // Returns an empty vector if the module is well-formed.
    // Errors reference no source span — they signal a compiler bug, not
    // user error, so diagnostics report them with an empty Span.
    [[nodiscard]] std::pmr::vector<ValidationError> validate_module(
        const MirModule &module, std::pmr::memory_resource *mr);

    void report_validation_errors(
        diagnostics::Engine *diag,
        const std::pmr::vector<ValidationError> &errors);
}
