//
// Created by antar on 21/10/26.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include <frappe/ast/program.hpp>
#include <frappe/checker/checker.hpp>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/lexer/lexer.hpp>
#include <frappe/parser/parser.hpp>
#include <frappe/resolver/resolver.hpp>

namespace frappe_test {
    struct Lex_Result {
        std::string source;
        std::unique_ptr<std::byte[]> backing;
        std::pmr::monotonic_buffer_resource arena;
        frappe::diagnostics::Engine engine;
        std::pmr::vector<frappe::syntax::Token> tokens;

        explicit Lex_Result(std::string_view src);

        [[nodiscard]] bool has_errors() const;

        [[nodiscard]] std::size_t error_count() const;
    };

    [[nodiscard]] std::unique_ptr<Lex_Result> lex(std::string_view source);

    struct Compile_Result {
        std::string source;
        std::unique_ptr<std::byte[]> backing;
        std::pmr::monotonic_buffer_resource arena;

        frappe::diagnostics::Engine engine;
        frappe::ast::Program program;
        std::optional<frappe::resolver::Resolver> resolver;
        std::optional<frappe::checker::Checker> checker;

        struct Error_Info {
            frappe::diagnostics::DiagnosticLevel level;
            std::uint32_t line;
            std::uint32_t column;
            std::string message;
        };

        std::vector<Error_Info> errors;

        explicit Compile_Result(std::string_view src);

        [[nodiscard]] bool has_errors() const { return !errors.empty(); }
        [[nodiscard]] std::size_t error_count() const { return errors.size(); }

        [[nodiscard]] std::size_t instance_count() const;

        [[nodiscard]] std::vector<std::string> instance_names() const;
    };

    [[nodiscard]] std::unique_ptr<Compile_Result> compile(std::string_view source);

    [[nodiscard]] std::unique_ptr<Compile_Result> compile_file(std::string_view path);

    void check_no_errors(const char *file, int line, const Compile_Result *r);

    void check_error_line(const char *file, int line, const Compile_Result *r,
                          std::size_t idx, std::uint32_t exp_line,
                          std::string_view exp_substr);

    void check_error_full(const char *file, int line, const Compile_Result *r,
                          std::size_t idx, std::uint32_t exp_line,
                          std::uint32_t exp_col, std::string_view exp_msg);

    [[nodiscard]] std::string project_path(std::string_view rel);
    //
    // Runs MIR construction and LLVM IR emission on a successful compile.
    // Returns std::nullopt if:
    //   - the compile had errors, or
    //   - MIR construction failed, or
    //   - LLVM IR emission failed, or
    //   - this binary was built without FRAPPE_LLVM=ON
    //
    // Callers should check the return against std::nullopt — on a non-LLVM
    // build this is the intended "skip" signal, not an error.
    [[nodiscard]] std::optional<std::string> build_ir(Compile_Result &r);

#define CHECK_NO_ERRORS(r) \
    ::frappe_test::check_no_errors(__FILE__, __LINE__, (r))

#define CHECK_ERROR_LINE(r, idx, line, substr) \
    ::frappe_test::check_error_line(__FILE__, __LINE__, (r), (idx), (line), (substr))

#define CHECK_ERROR_FULL(r, idx, line, col, msg) \
    ::frappe_test::check_error_full(__FILE__, __LINE__, (r), (idx), (line), (col), (msg))
}
