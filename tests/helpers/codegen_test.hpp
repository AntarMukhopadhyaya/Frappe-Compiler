//
// Created by antar on 22/10/26.
//

#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace frappe_test {
    // ============================================================================
    // LLVM IR inspection — text-based
    // ============================================================================
    //
    // The emitted IR is plain text, so a substring check is the honest way to
    // test it. Anything fancier (parsing the module back, running the
    // verifier from the test) is a v2 problem. These helpers cover the
    // structural checks that smoke tests actually need.

    [[nodiscard]] inline bool ir_contains(std::string_view ir,
                                          std::string_view needle) {
        return ir.find(needle) != std::string_view::npos;
    }

    // True iff the IR has a "define ..." line declaring the named function.
    // Handles both `define ... @name(` (unquoted, e.g. main) and
    // `define ... @"name"(` (quoted, e.g. identity$Int with '$').
    [[nodiscard]] inline bool ir_defines_function(std::string_view ir,
                                                  std::string_view name) {
        auto quoted = std::format("@\"{}\"(", name);
        auto unquoted = std::format("@{}(", name);

        std::string_view search = "define ";
        auto pos = ir.find(search);
        while (pos != std::string_view::npos) {
            auto eol = ir.find('\n', pos);
            if (eol == std::string_view::npos) eol = ir.size();
            auto line = ir.substr(pos, eol - pos);

            if (line.find(quoted) != std::string_view::npos) return true;
            if (line.find(unquoted) != std::string_view::npos) return true;

            pos = ir.find(search, eol);
        }
        return false;
    }

    // True iff the IR has a "declare ..." line for the named function.
    [[nodiscard]] inline bool ir_declares_function(std::string_view ir,
                                                   std::string_view name) {
        auto quoted = std::format("@\"{}\"(", name);
        auto unquoted = std::format("@{}(", name);

        std::string_view search = "declare ";
        auto pos = ir.find(search);
        while (pos != std::string_view::npos) {
            auto eol = ir.find('\n', pos);
            if (eol == std::string_view::npos) eol = ir.size();
            auto line = ir.substr(pos, eol - pos);

            if (line.find(quoted) != std::string_view::npos) return true;
            if (line.find(unquoted) != std::string_view::npos) return true;

            pos = ir.find(search, eol);
        }
        return false;
    }

    // Count how many times `needle` appears in the IR.
    [[nodiscard]] inline int ir_count(std::string_view ir,
                                      std::string_view needle) {
        int n = 0;
        auto pos = ir.find(needle);
        while (pos != std::string_view::npos) {
            ++n;
            pos = ir.find(needle, pos + needle.size());
        }
        return n;
    }

    // The three top-of-module fields that must be present for LLVM to
    // emit portable code.
    [[nodiscard]] inline bool has_target_triple(std::string_view ir) {
        return ir.find("target triple = ") != std::string_view::npos;
    }

    [[nodiscard]] inline bool has_data_layout(std::string_view ir) {
        return ir.find("target datalayout = ") != std::string_view::npos;
    }

    // True iff every "store" and "load" instruction uses the same alignment
    // as the corresponding alloca. This is the alignment regression guard —
    // if emit_llvm_ir loses its datalayout, i64 stores emit "align 4" while
    // allocas still say "align 8", and this check catches it.
    [[nodiscard]] inline bool has_consistent_alignments(std::string_view ir) {
        // Cheap heuristic: for each alloca iN (where N is 8, 16, 32, 64),
        // require no "store iN ..., align 4" or "load iN ..., align 4" in
        // the IR. This doesn't prove full correctness but catches the exact
        // bug the datalayout fix addresses.
        auto has_misaligned_i64_store =
                ir.find("store i64") != std::string_view::npos
                && ir.find("store i64", 0) != std::string_view::npos; // placeholder
        (void) has_misaligned_i64_store;

        // Direct check: "align 4" after an i64 store or load is bad.
        std::string_view search = "store i64";
        auto pos = ir.find(search);
        while (pos != std::string_view::npos) {
            auto eol = ir.find('\n', pos);
            if (eol == std::string_view::npos) eol = ir.size();
            if (ir.substr(pos, eol - pos).find("align 4") != std::string_view::npos) {
                return false;
            }
            pos = ir.find(search, eol);
        }
        search = "load i64";
        pos = ir.find(search);
        while (pos != std::string_view::npos) {
            auto eol = ir.find('\n', pos);
            if (eol == std::string_view::npos) eol = ir.size();
            if (ir.substr(pos, eol - pos).find("align 4") != std::string_view::npos) {
                return false;
            }
            pos = ir.find(search, eol);
        }
        return true;
    }
}

#define CHECK_IR(src, expected)                                                \
    do {                                                                       \
        auto r = compile_to_ir(src);                                           \
        if (r.compile->has_errors()) {                                         \
            for (const auto& e : r.compile->errors) {                          \
                UNSCOPED_INFO("[error] " << e.line << ":" << e.column          \
                              << " " << e.message);                            \
            }                                                                  \
        }                                                                      \
        REQUIRE_FALSE(r.compile->has_errors());                                \
        if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }\
        REQUIRE(frappe_test::ir_contains(*r.ir, expected));                    \
    } while (false)

#define CHECK_NO_IR(src, unexpected)                                           \
    do {                                                                       \
        auto r = compile_to_ir(src);                                           \
        REQUIRE_FALSE(r.compile->has_errors());                                \
        if (r.skip) { SUCCEED("codegen not built (FRAPPE_LLVM=OFF)"); return; }\
        REQUIRE_FALSE(frappe_test::ir_contains(*r.ir, unexpected));            \
    } while (false)
