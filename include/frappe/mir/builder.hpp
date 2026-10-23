//
// Created by antar on 21/10/26.
//

#pragma once

#include <cstdint>
#include <memory_resource>
#include <string_view>
#include <unordered_map>

#include <frappe/ast/program.hpp>
#include <frappe/checker/checker.hpp>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/mir/module.hpp>
#include <frappe/resolver/resolver.hpp>
#include <frappe/types/types.hpp>

namespace frappe::mir {
    struct Defer_Block {
        std::pmr::vector<MirInstruction> instructions;
    };

    struct MirBuilder {
        std::pmr::memory_resource *mr;
        frappe::checker::Checker *checker;
        frappe::diagnostics::Engine *diag;

        MirModule module_;
        MirFunction *current_fn = nullptr;
        const frappe::checker::Checked_Fn *current_inst = nullptr;
        const ast::ClassDecl *current_class = nullptr;
        const ast::FnDecl *current_decl = nullptr;

        std::pmr::unordered_map<const resolver::Symbol *, MirLocalId> scope;
        // LIFO stack of scopes. Each scope holds a LIFO stack of defer blocks.
        // Emitted at scope exit: reverse blocks, forward instructions per block.
        std::pmr::vector<std::pmr::vector<Defer_Block> > defer_stack;
        std::uint32_t next_temp = 0;
        std::uint32_t next_label = 0;
    };

    [[nodiscard]] MirModule build_module(
        const ast::Program &program,
        frappe::checker::Checker *checker,
        frappe::diagnostics::Engine *diag,
        std::pmr::memory_resource *mr);
}
