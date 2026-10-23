//
// Created by antar on 21/10/26.
//
#pragma once

#include <memory_resource>
#include <string>
#include <vector>
#include <frappe/mir/instruction.hpp>
#include <frappe/mir/local.hpp>
#include <frappe/mir/value.hpp>
#include <frappe/types/types.hpp>

namespace frappe::resolver {
    struct Symbol;
}

namespace frappe::mir {
    struct MirFunction {
        const resolver::Symbol *symbol;
        std::pmr::string name;
        const types::Type *return_type;
        std::pmr::vector<MirLocalId> parameters;
        std::pmr::vector<MirLocal> locals;
        std::pmr::vector<MirConstant> constants;
        std::pmr::vector<MirInstruction> instructions;
        // Type of each temporary, indexed by MirTemp ID.
        // Appended by the builder; used by codegen.
        std::pmr::vector<const types::Type *> temp_types;
    };
}
