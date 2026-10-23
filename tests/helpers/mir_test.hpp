//
// Created by antar on 22/10/26.
//

#pragma once

#include <cstddef>
#include <string_view>
#include <variant>
#include <set>

#include <frappe/mir/builder.hpp>
#include <frappe/mir/instruction.hpp>
#include <frappe/mir/module.hpp>

namespace frappe_test {
    [[nodiscard]] inline const frappe::mir::MirFunction *find_fn(
        const frappe::mir::MirModule &m, std::string_view name) {
        for (const auto &fn: m.functions) {
            if (fn.name == name) return &fn;
        }
        return nullptr;
    }

    [[nodiscard]] inline bool is_call_to(
        const frappe::mir::MirInstruction &inst, std::string_view target) {
        auto *call = std::get_if<frappe::mir::MirCall>(&inst);
        if (!call) return false;
        auto *direct = std::get_if<frappe::mir::MirDirectCallee>(&call->callee);
        return direct != nullptr && direct->name == target;
    }

    [[nodiscard]] inline bool is_return(const frappe::mir::MirInstruction &inst) {
        return std::holds_alternative<frappe::mir::MirReturn>(inst);
    }

    [[nodiscard]] inline bool is_panic(const frappe::mir::MirInstruction &inst) {
        return std::holds_alternative<frappe::mir::MirPanic>(inst);
    }

    [[nodiscard]] inline int count_calls_to(
        const frappe::mir::MirFunction &fn, std::string_view target) {
        int n = 0;
        for (const auto &inst: fn.instructions) {
            if (is_call_to(inst, target)) ++n;
        }
        return n;
    }

    // Returns index of last MirReturn, or fn.instructions.size() if none.
    [[nodiscard]] inline std::size_t last_return_index(
        const frappe::mir::MirFunction &fn) {
        for (std::size_t i = fn.instructions.size(); i-- > 0;) {
            if (is_return(fn.instructions[i])) return i;
        }
        return fn.instructions.size();
    }

    // Returns index of first MirPanic, or fn.instructions.size() if none.
    [[nodiscard]] inline std::size_t first_panic_index(
        const frappe::mir::MirFunction &fn) {
        for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
            if (is_panic(fn.instructions[i])) return i;
        }
        return fn.instructions.size();
    }

    // Returns index of first MirCall to `target`, or SIZE_MAX if none.
    [[nodiscard]] inline std::size_t first_call_index(
        const frappe::mir::MirFunction &fn, std::string_view target) {
        for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
            if (is_call_to(fn.instructions[i], target)) return i;
        }
        return static_cast<std::size_t>(-1);
    }

    [[nodiscard]] inline bool is_label(const frappe::mir::MirInstruction &i) {
        return std::holds_alternative<frappe::mir::MirLabelInstruction>(i);
    }

    [[nodiscard]] inline bool is_jump(const frappe::mir::MirInstruction &i) {
        return std::holds_alternative<frappe::mir::MirJump>(i);
    }

    [[nodiscard]] inline bool is_branch(const frappe::mir::MirInstruction &i) {
        return std::holds_alternative<frappe::mir::MirBranch>(i);
    }

    // Any instruction that ends a basic block.
    [[nodiscard]] inline bool is_terminator(const frappe::mir::MirInstruction &i) {
        return is_jump(i) || is_branch(i) || is_return(i) || is_panic(i);
    }

    [[nodiscard]] inline int count_labels(const frappe::mir::MirFunction &fn) {
        int n = 0;
        for (const auto &i: fn.instructions) if (is_label(i)) ++n;
        return n;
    }

    [[nodiscard]] inline int count_jumps(const frappe::mir::MirFunction &fn) {
        int n = 0;
        for (const auto &i: fn.instructions) if (is_jump(i)) ++n;
        return n;
    }

    [[nodiscard]] inline int count_branches(const frappe::mir::MirFunction &fn) {
        int n = 0;
        for (const auto &i: fn.instructions) if (is_branch(i)) ++n;
        return n;
    }

    // No instruction (other than a label) may follow a terminator.
    [[nodiscard]] inline bool is_well_formed(const frappe::mir::MirFunction &fn) {
        bool after_term = false;
        for (const auto &i: fn.instructions) {
            if (after_term) {
                if (!is_label(i)) return false;
                after_term = false;
            }
            if (is_terminator(i)) after_term = true;
        }
        return true;
    }

    // Every jump / branch target must be a defined label in the same function.
    [[nodiscard]] inline bool labels_defined(const frappe::mir::MirFunction &fn) {
        std::set<std::uint32_t> defined;
        for (const auto &i: fn.instructions) {
            if (auto *l = std::get_if<frappe::mir::MirLabelInstruction>(&i)) {
                defined.insert(l->label.index);
            }
        }
        for (const auto &i: fn.instructions) {
            if (auto *j = std::get_if<frappe::mir::MirJump>(&i)) {
                if (!defined.count(j->label.index)) return false;
            }
            if (auto *b = std::get_if<frappe::mir::MirBranch>(&i)) {
                if (!defined.count(b->then_label.index)) return false;
                if (!defined.count(b->else_label.index)) return false;
            }
        }
        return true;
    }
} // namespace frappe_test
