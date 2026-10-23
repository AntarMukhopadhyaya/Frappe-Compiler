//
// Created by antar on 21/10/26.
//


#include <frappe/mir/dump.hpp>

#include <cstdint>
#include <format>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <variant>

#include <frappe/mir/instruction.hpp>
#include <frappe/resolver/builtin.hpp>
#include <frappe/types/types.hpp>

namespace frappe::mir {
    namespace {
        using types::Type;

        // ============================================================================
        // Type and value writing
        // ============================================================================

        void write_type(std::pmr::string &out, const Type *t) {
            if (!t) {
                out += "<void>";
                return;
            }
            types::write_type_name(out, t);
        }

        // Resolves a MirValue to its printed form. Constants are inlined.
        // Locals show their name. Temps show `%N`.
        void write_value(std::pmr::string &out, const MirFunction &fn,
                         const MirValue &v) {
            std::visit([&](const auto &x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, MirTemp>) {
                    std::format_to(std::back_inserter(out), "%{}", x.index);
                } else if constexpr (std::is_same_v<T, MirLocalId>) {
                    if (x.index < fn.locals.size()) {
                        out += fn.locals[x.index].name;
                    } else {
                        std::format_to(std::back_inserter(out),
                                       "local#{}", x.index);
                    }
                } else if constexpr (std::is_same_v<T, MirConstantId>) {
                    if (x.index >= fn.constants.size()) {
                        std::format_to(std::back_inserter(out),
                                       "const#{}", x.index);
                        return;
                    }
                    const auto &c = fn.constants[x.index];
                    std::visit([&](const auto &cv) {
                        using C = std::decay_t<decltype(cv)>;
                        if constexpr (std::is_same_v<C, std::int64_t>) {
                            std::format_to(std::back_inserter(out), "{}", cv);
                        } else if constexpr (std::is_same_v<C, double>) {
                            std::format_to(std::back_inserter(out), "{}", cv);
                        } else if constexpr (std::is_same_v<C, bool>) {
                            out += cv ? "true" : "false";
                        } else if constexpr (std::is_same_v<C, std::string_view>) {
                            out += '"';
                            out += cv;
                            out += '"';
                        } else if constexpr (std::is_same_v<C, MirNil>) {
                            out += "nil";
                        }
                    }, c);
                }
            }, v);
        }

        // Convenience: write `%N:Type` for a temp destination.
        // Uses `temp_types[N]` when available, falls back to plain `%N`.
        void write_dest(std::pmr::string &out, const MirFunction &fn, MirTemp dest) {
            std::format_to(std::back_inserter(out), "%{}", dest.index);
            if (dest.index < fn.temp_types.size()) {
                out += ':';
                write_type(out, fn.temp_types[dest.index]);
            }
        }

        // ============================================================================
        // Binary / unary op strings
        // ============================================================================

        std::string_view bin_op_str(MirBinaryOp op) {
            switch (op) {
                case MirBinaryOp::Add: return "+";
                case MirBinaryOp::Sub: return "-";
                case MirBinaryOp::Mul: return "*";
                case MirBinaryOp::Div: return "/";
                case MirBinaryOp::Mod: return "%";
                case MirBinaryOp::Eq: return "==";
                case MirBinaryOp::Neq: return "!=";
                case MirBinaryOp::Lt: return "<";
                case MirBinaryOp::Lte: return "<=";
                case MirBinaryOp::Gt: return ">";
                case MirBinaryOp::Gte: return ">=";
            }
            return "?";
        }

        std::string_view un_op_str(MirUnaryOp op) {
            switch (op) {
                case MirUnaryOp::Neg: return "-";
                case MirUnaryOp::Not: return "!";
            }
            return "?";
        }

        // ============================================================================
        // Instruction printer
        // ============================================================================

        void write_call_args(std::pmr::string &out, const MirFunction &fn,
                             const std::pmr::vector<MirValue> &args) {
            out += '(';
            for (std::size_t k = 0; k < args.size(); ++k) {
                if (k > 0) out += ", ";
                write_value(out, fn, args[k]);
            }
            out += ')';
        }

        void write_instruction(std::pmr::string &out, const MirFunction &fn,
                               std::size_t idx, const MirInstruction &inst) {
            std::format_to(std::back_inserter(out), "  {}: ", idx);

            std::visit([&](const auto &i) {
                using T = std::decay_t<decltype(i)>;

                if constexpr (std::is_same_v<T, MirLoadConst>) {
                    write_dest(out, fn, i.destination);
                    out += " = ";
                    write_value(out, fn, MirValue{i.constant});
                } else if constexpr (std::is_same_v<T, MirLoadLocal>) {
                    write_dest(out, fn, i.destination);
                    out += " = ";
                    write_value(out, fn, MirValue{i.local});
                } else if constexpr (std::is_same_v<T, MirStoreLocal>) {
                    write_value(out, fn, MirValue{i.local});
                    out += " = ";
                    write_value(out, fn, i.value);
                } else if constexpr (std::is_same_v<T, MirMove>) {
                    write_dest(out, fn, i.destination);
                    out += " = ";
                    write_value(out, fn, i.value);
                } else if constexpr (std::is_same_v<T, MirBinary>) {
                    write_dest(out, fn, i.destination);
                    std::format_to(std::back_inserter(out), " = ");
                    write_value(out, fn, i.left);
                    std::format_to(std::back_inserter(out), " {} ", bin_op_str(i.op));
                    write_value(out, fn, i.right);
                } else if constexpr (std::is_same_v<T, MirUnary>) {
                    write_dest(out, fn, i.destination);
                    std::format_to(std::back_inserter(out), " = {}", un_op_str(i.op));
                    write_value(out, fn, i.operand);
                } else if constexpr (std::is_same_v<T, MirLabelInstruction>) {
                    std::format_to(std::back_inserter(out), "L{}:", i.label.index);
                } else if constexpr (std::is_same_v<T, MirJump>) {
                    std::format_to(std::back_inserter(out), "jump L{}", i.label.index);
                } else if constexpr (std::is_same_v<T, MirBranch>) {
                    out += "branch ";
                    write_value(out, fn, i.condition);
                    std::format_to(std::back_inserter(out), ", L{}, L{}",
                                   i.then_label.index, i.else_label.index);
                } else if constexpr (std::is_same_v<T, MirCall>) {
                    if (i.destination) {
                        write_dest(out, fn, *i.destination);
                        out += " = ";
                    }
                    if (i.is_virtual) {
                        out += "vcall [slot ";
                        out += std::to_string(*i.vtable_slot);
                        out += "] ";
                    } else {
                        out += "call ";
                    }
                    std::visit([&]<typename T0>(const T0 &c) {
                        using C = std::decay_t<T0>;
                        if constexpr (std::is_same_v<C, MirDirectCallee>) {
                            out += c.name;
                        } else {
                            write_value(out, fn, c);
                        }
                    }, i.callee);

                    write_call_args(out, fn, i.args);
                } else if constexpr (std::is_same_v<T, MirReturn>) {
                    out += "return";
                    if (i.value) {
                        out += ' ';
                        write_value(out, fn, *i.value);
                    }
                } else if constexpr (std::is_same_v<T, MirPanic>) {
                    out += "panic ";
                    write_value(out, fn, i.message);
                } else if constexpr (std::is_same_v<T, MirNewObject>) {
                    write_dest(out, fn, i.destination);
                    std::format_to(std::back_inserter(out), " = new {}",
                                   i.class_name);
                    write_call_args(out, fn, i.args);
                } else if constexpr (std::is_same_v<T, MirBuildArray>) {
                    write_dest(out, fn, i.destination);
                    out += " = [";
                    for (std::size_t k = 0; k < i.elements.size(); ++k) {
                        if (k > 0) out += ", ";
                        write_value(out, fn, i.elements[k]);
                    }
                    out += ']';
                } else if constexpr (std::is_same_v<T, MirGetField>) {
                    write_dest(out, fn, i.destination);
                    out += " = ";
                    write_value(out, fn, i.object);
                    std::format_to(std::back_inserter(out), ".{}", i.field_name);
                } else if constexpr (std::is_same_v<T, MirSetField>) {
                    write_value(out, fn, i.object);
                    std::format_to(std::back_inserter(out), ".{} = ", i.field_name);
                    write_value(out, fn, i.value);
                } else if constexpr (std::is_same_v<T, MirGetIndex>) {
                    write_dest(out, fn, i.destination);
                    out += " = ";
                    write_value(out, fn, i.object);
                    out += '[';
                    write_value(out, fn, i.index);
                    out += ']';
                } else if constexpr (std::is_same_v<T, MirSetIndex>) {
                    write_value(out, fn, i.object);
                    out += '[';
                    write_value(out, fn, i.index);
                    out += "] = ";
                    write_value(out, fn, i.value);
                }
            }, inst);

            out += '\n';
        }

        // ============================================================================
        // Function printer
        // ============================================================================

        void write_function(std::pmr::string &out, const MirFunction &fn) {
            out += "function ";
            out += fn.name;
            out += '(';
            for (std::size_t i = 0; i < fn.parameters.size(); ++i) {
                if (i > 0) out += ", ";
                auto lid = fn.parameters[i];
                if (lid.index < fn.locals.size()) {
                    const auto &loc = fn.locals[lid.index];
                    out += loc.name;
                    out += ": ";
                    write_type(out, loc.type);
                } else {
                    std::format_to(std::back_inserter(out),
                                   "param#{}(?)", lid.index);
                }
            }
            out += ") -> ";
            write_type(out, fn.return_type);
            out += " {\n";

            for (std::size_t i = 0; i < fn.instructions.size(); ++i) {
                write_instruction(out, fn, i, fn.instructions[i]);
            }

            out += "}\n\n";
        }

        // ============================================================================
        // Class printer
        // ============================================================================

        void write_class(std::pmr::string &out, const MirClass &mc) {
            std::format_to(std::back_inserter(out), "class {}", mc.name);
            if (mc.base_name) {
                std::format_to(std::back_inserter(out),
                               " extends {}", *mc.base_name);
            }
            out += " {\n";

            for (const auto &f: mc.fields) {
                std::format_to(std::back_inserter(out),
                               "  field[{}] {}: ", f.index, f.name);
                write_type(out, f.type);
                out += '\n';
            }

            for (const auto &m: mc.methods) {
                std::format_to(std::back_inserter(out),
                               "  method {} -> {}\n", m.name, m.fn_name);
            }

            out += "}\n\n";
        }
    } // namespace

    // ============================================================================
    // Public
    // ============================================================================

    std::pmr::string dump_module(const MirModule &module,
                                 std::pmr::memory_resource *mr) {
        std::pmr::string out{mr};
        for (const auto &mc: module.classes) write_class(out, mc);
        for (const auto &fn: module.functions) write_function(out, fn);
        return out;
    }
} // namespace frappe::mir
