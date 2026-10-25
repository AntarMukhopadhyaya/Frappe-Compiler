//
// Created by antar on 21/10/26.
//
#include "compile.hpp"
#include <frappe/codegen/codegen.hpp>
#include <frappe/mir/builder.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace frappe_test {
    namespace {
        constexpr std::size_t ARENA_SIZE = 4 * 1024 * 1024;

        std::string to_std_string(const std::pmr::string &s) {
            return std::string{s.data(), s.size()};
        }

        void collect_errors(Compile_Result *r) {
            r->errors.clear();
            for (const auto &d: r->engine.diagnostics) {
                r->errors.push_back({
                    .level = d.level,
                    .line = d.span.start.line,
                    .column = d.span.start.column,
                    .message = to_std_string(d.message),
                });
            }
        }

        // Pretty-print every error for a failure message.
        std::string format_all_errors(const Compile_Result *r) {
            if (r->errors.empty()) return "  (no errors)";
            std::string out;
            for (std::size_t i = 0; i < r->errors.size(); ++i) {
                const auto &e = r->errors[i];
                out += std::format("  [{}] [{}:{}] {}\n",
                                   i, e.line, e.column, e.message);
            }
            return out;
        }
    } // namespace

    // ============================================================================
    // Lex_Result
    // ============================================================================

    Lex_Result::Lex_Result(std::string_view src)
        : source{src}
          , backing{std::make_unique<std::byte[]>(ARENA_SIZE)}
          , arena{backing.get(), ARENA_SIZE}
          , engine{frappe::diagnostics::make_engine(&arena)}
          , tokens{&arena} {
    }

    bool Lex_Result::has_errors() const {
        return frappe::diagnostics::has_errors(&engine);
    }

    std::size_t Lex_Result::error_count() const {
        return engine.diagnostics.size();
    }

    std::unique_ptr<Lex_Result> lex(std::string_view source) {
        auto r = std::make_unique<Lex_Result>(source);
        auto lexer = frappe::lexer::make_lexer(r->source, &r->engine, &r->arena);
        static_cast<void>(frappe::lexer::tokenize(&lexer));
        r->tokens = std::move(lexer.tokens); // same arena — pointer transfer
        return r;
    }

    // ============================================================================
    // Compile_Result
    // ============================================================================

    Compile_Result::Compile_Result(std::string_view src)
        : source{src}
          , backing{std::make_unique<std::byte[]>(ARENA_SIZE)}
          , arena{backing.get(), ARENA_SIZE}
          , engine{frappe::diagnostics::make_engine(&arena)}
          , program{
              frappe::source::Span{},
              std::pmr::vector<frappe::ast::Decl *>{&arena}
          }
          , resolver{std::nullopt}
          , checker{std::nullopt}
          , errors{} {
    }

    std::size_t Compile_Result::instance_count() const {
        return checker ? checker->instances.size() : 0;
    }

    std::vector<std::string> Compile_Result::instance_names() const {
        std::vector<std::string> names;
        if (!checker) return names;
        for (const auto &[name, _]: checker->instances) {
            names.push_back(std::string{name}); // ← explicit construction
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::unique_ptr<Compile_Result> compile(std::string_view src) {
        auto r = std::make_unique<Compile_Result>(src);

        auto lexer = frappe::lexer::make_lexer(r->source, &r->engine, &r->arena);
        auto tokens = frappe::lexer::tokenize(&lexer);
        if (frappe::diagnostics::has_errors(&r->engine)) {
            collect_errors(r.get());
            return r;
        }

        r->program = frappe::parser::parse(tokens, &r->engine, &r->arena);
        if (frappe::diagnostics::has_errors(&r->engine)) {
            collect_errors(r.get());
            return r;
        }

        r->resolver.emplace(
            frappe::resolver::make_resolver(&r->engine, &r->arena));
        frappe::resolver::resolve_program(&*r->resolver, r->program);
        if (frappe::diagnostics::has_errors(&r->engine)) {
            collect_errors(r.get());
            return r;
        }

        r->checker.emplace(
            frappe::checker::make_checker(&*r->resolver, &r->engine, &r->arena));
        frappe::checker::check_program(&*r->checker, r->program);

        collect_errors(r.get());
        return r;
    }

    std::unique_ptr<Compile_Result> compile_file(std::string_view path) {
        std::ifstream f{std::string{path}, std::ios::binary};
        if (!f) return nullptr;
        std::ostringstream buf;
        buf << f.rdbuf();
        return compile(buf.str());
    }

    // ============================================================================
    // Assertion helpers
    // ============================================================================

    void check_no_errors(const char *file, int line, const Compile_Result *r) {
        if (r->has_errors()) {
            FAIL(file << ":" << line << ": expected no errors, got "
                << r->error_count() << ":\n"
                << format_all_errors(r));
        }
    }

    void check_error_line(const char *file, int line, const Compile_Result *r,
                          std::size_t idx, std::uint32_t exp_line,
                          std::string_view exp_substr) {
        if (idx >= r->errors.size()) {
            FAIL(file << ":" << line
                << ": expected error[" << idx << "], only "
                << r->errors.size() << " present:\n"
                << format_all_errors(r));
        }
        const auto &e = r->errors[idx];
        if (e.line != exp_line
            || e.message.find(exp_substr) == std::string::npos) {
            FAIL(file << ":" << line << ": error[" << idx << "] mismatch:\n"
                << "  actual:   [" << e.line << ":" << e.column << "] "
                << e.message << "\n"
                << "  expected: line " << exp_line
                << ", message containing '" << exp_substr << "'\n"
                << "  all errors:\n"
                << format_all_errors(r));
        }
    }

    void check_error_full(const char *file, int line, const Compile_Result *r,
                          std::size_t idx, std::uint32_t exp_line,
                          std::uint32_t exp_col, std::string_view exp_msg) {
        if (idx >= r->errors.size()) {
            FAIL(file << ":" << line
                << ": expected error[" << idx << "], only "
                << r->errors.size() << " present:\n"
                << format_all_errors(r));
        }
        const auto &e = r->errors[idx];
        if (e.line != exp_line || e.column != exp_col || e.message != exp_msg) {
            FAIL(file << ":" << line << ": error[" << idx << "] mismatch:\n"
                << "  actual:   [" << e.line << ":" << e.column << "] "
                << e.message << "\n"
                << "  expected: [" << exp_line << ":" << exp_col << "] "
                << exp_msg << "\n"
                << "  all errors:\n"
                << format_all_errors(r));
        }
    }

    std::string project_path(std::string_view rel) {
        return std::format("{}/{}", FRAPPE_PROJECT_ROOT, rel);
    }

    // tests/helpers/compile.cpp
    std::optional<std::string> build_ir(Compile_Result &r) {
        if (r.has_errors()) return std::nullopt;

        auto mir = frappe::mir::build_module(
            r.program, &*r.checker, &r.engine, &r.arena);
        if (frappe::diagnostics::has_errors(&r.engine)) {
            collect_errors(&r);
            return std::nullopt;
        }

#ifndef FRAPPE_LLVM_ENABLED
        (void) mir;
        return std::nullopt;   // ← only legitimate nullopt when LLVM is off
#else
        auto ir = frappe::codegen::emit_llvm_ir(
            mir,
            frappe::codegen::EmitOptions{.module_name = "test"},
            &r.engine);
        if (frappe::diagnostics::has_errors(&r.engine)) {
            collect_errors(&r);   // ← capture LLVM error into r.errors
            return std::nullopt;   // caller can distinguish via has_errors()
        }
        return ir;
#endif
    }
}
