//
// Created by antar on 23/10/26.
//

#include <frappe/mir/validate.hpp>
#include <format>
#include <iterator>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

#include <frappe/diagnostics/engine.hpp>
#include <frappe/mir/function.hpp>
#include <frappe/mir/instruction.hpp>
#include <frappe/mir/module.hpp>
#include <frappe/mir/value.hpp>


namespace frappe::mir {
    namespace {
        template<typename... Ts>
        struct overloaded : Ts... {
            using Ts::operator()...;
        };

        template<typename... Ts>
        overloaded(Ts...) -> overloaded<Ts...>;

        constexpr std::size_t MAX_ERRORS = 100;

        struct FnContext {
            const MirFunction *fn;
            std::string_view name;
        };

        void push_error(std::pmr::vector<ValidationError> &errors,
                        std::pmr::memory_resource *mr,
                        std::string_view fn_name,
                        std::pmr::string msg) {
            if (errors.size() >= MAX_ERRORS) return;
            std::pmr::string name_copy{fn_name.data(), fn_name.size(), mr};
            errors.push_back({std::move(name_copy), std::move(msg)});
        }

        void check_temp(MirTemp t, const FnContext &ctx,
                        std::pmr::vector<ValidationError> &errors,
                        std::pmr::memory_resource *mr) {
            if (t.index >= ctx.fn->temp_types.size()) {
                std::pmr::string msg{mr};
                std::format_to(std::back_inserter(msg),
                               "temp %{} out of range (function has {} temps)",
                               t.index, ctx.fn->temp_types.size());
                push_error(errors, mr, ctx.name, std::move(msg));
            }
        }

        void check_local(MirLocalId l, const FnContext &ctx,
                         std::pmr::vector<ValidationError> &errors,
                         std::pmr::memory_resource *mr) {
            if (l.index >= ctx.fn->locals.size()) {
                std::pmr::string msg{mr};
                std::format_to(std::back_inserter(msg),
                               "local %{} out of range (function has {} locals)",
                               l.index, ctx.fn->locals.size());
                push_error(errors, mr, ctx.name, std::move(msg));
            }
        }

        void check_constant(MirConstantId c, const FnContext &ctx,
                            std::pmr::vector<ValidationError> &errors,
                            std::pmr::memory_resource *mr) {
            if (c.index >= ctx.fn->constants.size()) {
                std::pmr::string msg{mr};
                std::format_to(std::back_inserter(msg),
                               "constant %{} out of range (function has {} constants)",
                               c.index, ctx.fn->constants.size());
                push_error(errors, mr, ctx.name, std::move(msg));
            }
        }

        void check_value(const MirValue &v, const FnContext &ctx,
                         std::pmr::vector<ValidationError> &errors,
                         std::pmr::memory_resource *mr) {
            std::visit(overloaded{
                           [&](MirTemp t) { check_temp(t, ctx, errors, mr); },
                           [&](MirLocalId l) { check_local(l, ctx, errors, mr); },
                           [&](MirConstantId c) { check_constant(c, ctx, errors, mr); },
                       }, v);
        }

        bool is_terminator(const MirInstruction &i) {
            return std::holds_alternative<MirReturn>(i)
                   || std::holds_alternative<MirJump>(i)
                   || std::holds_alternative<MirBranch>(i)
                   || std::holds_alternative<MirPanic>(i);
        }

        void check_function(const MirFunction &fn,
                            std::pmr::vector<ValidationError> &errors,
                            std::pmr::memory_resource *mr) {
            FnContext ctx{&fn, fn.name};
            // Parameters must reference valid locals
            for (auto p: fn.parameters) {
                check_local(p, ctx, errors, mr);
            }
            // Build label set for this function
            std::pmr::unordered_set<std::uint32_t> labels{mr};
            for (const auto &inst: fn.instructions) {
                if (auto *l = std::get_if<MirLabelInstruction>(&inst)) {
                    labels.insert(l->label.index);
                }
            }
            bool after_terminator = false;
            for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
                const auto &inst = fn.instructions[i];
                if (after_terminator) {
                    if (!std::holds_alternative<MirLabelInstruction>(inst)) {
                        std::pmr::string msg{mr};
                        std::format_to(std::back_inserter(msg),
                                       "instruction {} follows a terminator "
                                       "without an intervening label",
                                       i);
                        push_error(errors, mr, ctx.name, std::move(msg));
                    } else {
                        after_terminator = false;
                    }
                }
                std::visit(overloaded{
                               [&](const MirLoadConst &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_constant(x.constant, ctx, errors, mr);
                               },
                               [&](const MirLoadLocal &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_local(x.local, ctx, errors, mr);
                               },
                               [&](const MirStoreLocal &x) {
                                   check_local(x.local, ctx, errors, mr);
                                   check_value(x.value, ctx, errors, mr);
                               },
                               [&](const MirMove &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_value(x.value, ctx, errors, mr);
                               },
                               [&](const MirBinary &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_value(x.left, ctx, errors, mr);
                                   check_value(x.right, ctx, errors, mr);
                               },
                               [&](const MirUnary &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_value(x.operand, ctx, errors, mr);
                               },
                               [&](const MirLabelInstruction &) {
                                   // Label set already built; nothing to verify here.
                               },
                               [&](const MirJump &x) {
                                   if (!labels.contains(x.label.index)) {
                                       std::pmr::string msg{mr};
                                       std::format_to(std::back_inserter(msg),
                                                      "jump to undefined label L{}",
                                                      x.label.index);
                                       push_error(errors, mr, ctx.name, std::move(msg));
                                   }
                               },
                               [&](const MirBranch &x) {
                                   check_value(x.condition, ctx, errors, mr);
                                   if (!labels.contains(x.then_label.index)) {
                                       std::pmr::string msg{mr};
                                       std::format_to(std::back_inserter(msg),
                                                      "branch to undefined label L{}",
                                                      x.then_label.index);
                                       push_error(errors, mr, ctx.name, std::move(msg));
                                   }
                                   if (!labels.contains(x.else_label.index)) {
                                       std::pmr::string msg{mr};
                                       std::format_to(std::back_inserter(msg),
                                                      "branch to undefined label L{}",
                                                      x.else_label.index);
                                       push_error(errors, mr, ctx.name, std::move(msg));
                                   }
                               },
                               [&](const MirCall &x) {
                                   if (x.destination) check_temp(*x.destination, ctx, errors, mr);
                                   if (auto *v = std::get_if<MirValue>(&x.callee)) {
                                       check_value(*v, ctx, errors, mr);
                                   }
                                   for (const auto &a: x.args) check_value(a, ctx, errors, mr);
                                   if (x.is_virtual && !x.vtable_slot.has_value()) {
                                       std::pmr::string msg{mr};
                                       std::format_to(std::back_inserter(msg),
                                                      "is_virtual set but vtable_slot missing");
                                       push_error(errors, mr, ctx.name, std::move(msg));
                                   }
                                   if (!x.is_virtual && x.vtable_slot.has_value()) {
                                       std::pmr::string msg{mr};
                                       std::format_to(std::back_inserter(msg),
                                                      "vtable_slot set on non-virtual call");
                                       push_error(errors, mr, ctx.name, std::move(msg));
                                   }
                               },
                               [&](const MirReturn &x) {
                                   if (x.value) check_value(*x.value, ctx, errors, mr);
                               },
                               [&](const MirPanic &x) {
                                   check_value(x.message, ctx, errors, mr);
                               },
                               [&](const MirNewObject &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   for (const auto &a: x.args) check_value(a, ctx, errors, mr);
                               },
                               [&](const MirBuildArray &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   for (const auto &e: x.elements) check_value(e, ctx, errors, mr);
                               },
                               [&](const MirGetField &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_value(x.object, ctx, errors, mr);
                               },
                               [&](const MirSetField &x) {
                                   check_value(x.object, ctx, errors, mr);
                                   check_value(x.value, ctx, errors, mr);
                               },
                               [&](const MirGetIndex &x) {
                                   check_temp(x.destination, ctx, errors, mr);
                                   check_value(x.object, ctx, errors, mr);
                                   check_value(x.index, ctx, errors, mr);
                               },
                               [&](const MirSetIndex &x) {
                                   check_value(x.object, ctx, errors, mr);
                                   check_value(x.index, ctx, errors, mr);
                                   check_value(x.value, ctx, errors, mr);
                               },

                           }, inst);
                if (is_terminator(inst)) after_terminator = true;
            }
            // Last instruction must be a terminator
            if (!fn.instructions.empty() && !is_terminator(fn.instructions.back())) {
                std::pmr::string msg{mr};
                std::format_to(std::back_inserter(msg),
                               "function ends without a terminator "
                               "(last instruction is not return/jump/branch/panic)");
                push_error(errors, mr, ctx.name, std::move(msg));
            }
        }
    }

    std::pmr::vector<ValidationError>
    validate_module(const MirModule &module, std::pmr::memory_resource *mr) {
        std::pmr::vector<ValidationError> errors{mr};
        for (const auto &fn: module.functions) {
            check_function(fn, errors, mr);
        }
        return errors;
    }

    void report_validation_errors(diagnostics::Engine *diag, const std::pmr::vector<ValidationError> &errors) {
        for (const auto &e: errors) {
            diagnostics::report_error(diag, {},
                                      "MIR validation error in '{}': {}",
                                      e.function_name, e.message);
        }
    }
}
