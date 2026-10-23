//
// Created by antar on 21/10/26.
//

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <memory_resource>
#include <vector>


#include <frappe/mir/value.hpp>


namespace frappe::resolver {
    struct Symbol;
}

namespace frappe::mir {
    struct MirLoadConst {
        MirTemp destination;
        MirConstantId constant;
    };

    struct MirLoadLocal {
        MirTemp destination;
        MirLocalId local;
    };

    struct MirStoreLocal {
        MirLocalId local;
        MirValue value;
    };

    struct MirMove {
        MirTemp destination;
        MirValue value;
    };

    enum class MirBinaryOp : std::uint8_t {
        Add, Sub, Mul, Div, Mod,
        Eq, Neq, Lt, Lte, Gt, Gte,
    };

    struct MirBinary {
        MirBinaryOp op;
        MirTemp destination;
        MirValue left;
        MirValue right;
    };

    enum class MirUnaryOp : std::uint8_t { Neg, Not };

    struct MirUnary {
        MirUnaryOp op;
        MirTemp destination;
        MirValue operand;
    };

    struct MirLabelInstruction {
        MirLabel label;
    };

    struct MirJump {
        MirLabel label;
    };

    struct MirBranch {
        MirValue condition;
        MirLabel then_label;
        MirLabel else_label;
    };

    struct MirDirectCallee {
        std::string_view name;
    };

    using MirCallee = std::variant<MirDirectCallee, MirValue>;

    struct MirCall {
        std::optional<MirTemp> destination; // nullopt for void returing calls
        MirCallee callee;
        std::pmr::vector<MirValue> args;
    };

    struct MirReturn {
        std::optional<MirValue> value;
    };

    struct MirNewObject {
        MirTemp destination;
        std::string_view class_name;
        std::pmr::vector<MirValue> args;
    };

    struct MirBuildArray {
        MirTemp destination;
        std::pmr::vector<MirValue> elements;
    };

    struct MirGetField {
        MirTemp destination;
        MirValue object;
        std::string_view field_name;
    };

    struct MirSetField {
        MirValue object;
        std::string_view field_name;
        MirValue value;
    };

    struct MirGetIndex {
        MirTemp destination;
        MirValue object;
        MirValue index;
    };

    struct MirSetIndex {
        MirValue object;
        MirValue index;
        MirValue value;
    };

    struct MirPanic {
        MirValue message;
    };

    using MirInstruction = std::variant<
        MirLoadConst, MirLoadLocal, MirStoreLocal, MirMove,
        MirBinary, MirUnary,
        MirLabelInstruction, MirJump, MirBranch,
        MirCall, MirReturn,
        MirPanic,
        MirNewObject,
        MirBuildArray,
        MirGetField, MirSetField,
        MirGetIndex, MirSetIndex
    >;
}
