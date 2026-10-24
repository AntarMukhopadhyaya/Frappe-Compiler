//
// Created by antar on 24/10/26.
//
//
// Compiler intrinsics — functions available in every Frappe program without
// being declared. `println` is the first. The table is data, not code: adding
// an intrinsic means adding a row, not a special case in the checker.
//
// Dispatch model:
//   1. check_call sees an Ident callee.
//   2. Resolver lookup first — user-declared `println` shadows the intrinsic.
//   3. If no user symbol, consult this table.
//   4. If an intrinsic matches, check_intrinsic_call handles it uniformly.
//
// No changes needed to resolver, MIR, or codegen. Intrinsics are dispatched
// by `record_call_name` + a `MirDirectCallee` to the runtime's C function —
// indistinguishable from a normal function call once the checker is done.


#pragma once

#include <cstddef>
#include <string_view>

#include <frappe/resolver/builtin.hpp>

namespace frappe::resolver {
    struct IntrinsicOverload {
        Builtin arg_type; // the type this overload accepts
        std::string_view runtime_name; // The C function MIR/codegen will call.
    };

    struct Intrinsic {
        std::string_view name;
        std::size_t arg_count; // v1: always 1. reserved for future 0-arg intrinsics
        const IntrinsicOverload *overloads;
        std::size_t overload_count;
    };

    // Returns nullptr if `name` is not a declared intrinsic.
    [[nodiscard]] const Intrinsic *find_intrinsic(std::string_view name) noexcept;
}
