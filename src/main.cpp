#include <frappe/ast_printer/printer.hpp>
#include <frappe/checker/checker.hpp>
#include <frappe/codegen/codegen.hpp>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/lexer/lexer.hpp>
#include <frappe/parser/parser.hpp>
#include <frappe/resolver/resolver.hpp>
#include <frappe/mir/dump.hpp>
#include <frappe/mir/builder.hpp>

#include <cstdio>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {
    struct Options {
        std::string input;
        bool dump_tokens = false;
        bool dump_ast = false;
        bool dump_mir = false;
        bool dump_llvm_ir = false;
        frappe::codegen::OptLevel opt_level = frappe::codegen::OptLevel::None;
    };

    bool parse_args(int argc, char **argv, Options *out) {
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--tokens") {
                out->dump_tokens = true;
            } else if (arg == "--ast") {
                out->dump_ast = true;
            } else if (arg == "--mir") {
                out->dump_mir = true;
            } else if (arg == "--llvm-ir") {
                out->dump_llvm_ir = true;
            } else if (arg.starts_with("--opt=")) {
                auto level = arg.substr(6);
                if (level == "0") out->opt_level = frappe::codegen::OptLevel::None;
                else if (level == "1") out->opt_level = frappe::codegen::OptLevel::Less;
                else if (level == "2") out->opt_level = frappe::codegen::OptLevel::Default;
                else if (level == "3") out->opt_level = frappe::codegen::OptLevel::Aggressive;
                else {
                    std::fprintf(stderr, "invalid --opt level: %.*s (use 0|1|2|3)\n",
                                 static_cast<int>(level.size()), level.data());
                    return false;
                }
            } else if (!arg.empty() && arg[0] == '-') {
                std::fprintf(stderr, "unknown flag: %.*s\n",
                             static_cast<int>(arg.size()), arg.data());
                return false;
            } else {
                if (!out->input.empty()) {
                    std::fprintf(stderr, "only one input file may be given\n");
                    return false;
                }
                out->input = std::string{arg};
            }
        }
        if (out->input.empty()) {
            std::fprintf(stderr,
                         "usage: frappe [--tokens|--ast|--mir|--llvm-ir] <source_file>\n");
            return false;
        }
        return true;
    }

    void dump_tokens(std::span<const frappe::syntax::Token> toks) {
        for (const auto &t: toks) {
            std::printf("[%u:%u] kind=%u lexeme='%.*s'\n",
                        static_cast<unsigned>(t.span.start.line),
                        static_cast<unsigned>(t.span.start.column),
                        static_cast<unsigned>(t.kind),
                        static_cast<int>(t.lexeme.size()),
                        t.lexeme.data());
        }
    }
} // namespace

int main(int argc, char **argv) {
    Options opts;
    if (!parse_args(argc, argv, &opts)) return 1;

    std::string source;
    {
        std::ifstream f(opts.input, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "failed to open file '%s'\n", opts.input.c_str());
            return 1;
        }
        std::ostringstream buf;
        buf << f.rdbuf();
        source = std::move(buf).str();
    }

    auto backing = std::make_unique<std::byte[]>(4 * 1024 * 1024);
    std::pmr::monotonic_buffer_resource arena(backing.get(), 4 * 1024 * 1024);

    auto engine = frappe::diagnostics::make_engine(&arena);

    auto lexer = frappe::lexer::make_lexer(source, &engine, &arena);
    auto tokens = frappe::lexer::tokenize(&lexer);
    if (frappe::diagnostics::has_errors(&engine)) {
        frappe::diagnostics::print(&engine);
        return 1;
    }

    if (opts.dump_tokens) {
        dump_tokens(tokens);
        return 0;
    }

    auto program = frappe::parser::parse(tokens, &engine, &arena);
    if (frappe::diagnostics::has_errors(&engine)) {
        frappe::diagnostics::print(&engine);
        return 1;
    }

    auto res = frappe::resolver::make_resolver(&engine, &arena);
    frappe::resolver::resolve_program(&res, program);
    if (frappe::diagnostics::has_errors(&engine)) {
        frappe::diagnostics::print(&engine);
        return 1;
    }

    auto chk = frappe::checker::make_checker(&res, &engine, &arena);
    frappe::checker::check_program(&chk, program);

    if (opts.dump_ast) {
        auto printer = frappe::ast_printer::make_printer(&arena);
        frappe::ast_printer::dump_program(&printer, program);
        std::fwrite(printer.buf.data(), 1, printer.buf.size(), stdout);
        std::fflush(stdout);
    }

    if (frappe::diagnostics::has_errors(&engine)) {
        frappe::diagnostics::print(&engine);
        return 1;
    }

    if (!opts.dump_mir && !opts.dump_llvm_ir) {
        if (!opts.dump_ast) {
            std::printf("checked %zu decls\n", program.decls.size());
        }
        return 0;
    }

    auto mod = frappe::mir::build_module(program, &chk, &engine, &arena);
    if (frappe::diagnostics::has_errors(&engine)) {
        frappe::diagnostics::print(&engine);
        return 1;
    }

    if (opts.dump_mir) {
        auto text = frappe::mir::dump_module(mod, &arena);
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fflush(stdout);
        if (opts.dump_llvm_ir) std::fputs("\n", stdout); // visual separator
    }

    if (opts.dump_llvm_ir) {
        auto ir = frappe::codegen::emit_llvm_ir(
            mod,
            frappe::codegen::EmitOptions{.module_name = opts.input, .opt_level = opts.opt_level},
            &engine);
        if (frappe::diagnostics::has_errors(&engine)) {
            frappe::diagnostics::print(&engine);
            return 1;
        }
        std::fwrite(ir.data(), 1, ir.size(), stdout);
        std::fflush(stdout);
    }

    return 0;
}
