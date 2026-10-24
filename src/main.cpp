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
#include <cstdlib>
#include <fstream>
#include <memory>
#include <memory_resource>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>

namespace {
    struct Options {
        std::string input;
        std::string output;
        bool dump_tokens  = false;
        bool dump_ast     = false;
        bool dump_mir     = false;
        bool dump_llvm_ir = false;
        bool run          = false;
        frappe::codegen::OptLevel opt_level = frappe::codegen::OptLevel::None;
    };

    void usage() {
        std::fprintf(stderr,
            "usage: frappe [options] <source_file>\n"
            "  --tokens             dump tokens and exit\n"
            "  --ast                dump AST (then continue)\n"
            "  --mir                dump MIR and exit\n"
            "  --llvm-ir            dump LLVM IR and exit\n"
            "  --opt=0|1|2|3        optimization level (default 0)\n"
            "  -o, --emit <path>    compile to executable at <path>\n"
            "  --run                compile to a temp executable and run it\n");
    }

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
            } else if (arg == "--run") {
                out->run = true;
            } else if (arg == "-o" || arg == "--emit") {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "%s requires an argument\n",
                                 std::string(arg).c_str());
                    return false;
                }
                out->output = argv[++i];
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
            usage();
            return false;
        }
        if (out->run && !out->output.empty()) {
            std::fprintf(stderr, "--run and -o are mutually exclusive\n");
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

    std::string make_temp_exe_path() {
        const char* tmp = std::getenv("TMPDIR");
        std::string dir = tmp ? tmp : "/tmp";
        dir += "/frappe_";
        dir += std::to_string(::getpid());
        return dir;
    }

    int run_executable(const std::string& path) {
        int rc = std::system(path.c_str());
        if (rc == -1) {
            std::fprintf(stderr, "frappe: failed to invoke %s\n", path.c_str());
            return 1;
        }
        if ((rc & 0x7f) != 0) {
            int sig = rc & 0x7f;
            std::fprintf(stderr, "frappe: program terminated by signal %d%s\n",
                         sig,
                         sig == 11 ? " (SIGSEGV — likely out-of-bounds memory access)" : "");
            return 128 + sig;
        }
        return (rc >> 8) & 0xff;
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

    auto lexer  = frappe::lexer::make_lexer(source, &engine, &arena);
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

    const bool need_mir = opts.dump_mir || opts.dump_llvm_ir
                       || opts.run      || !opts.output.empty();
    if (!need_mir) {
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
        if (opts.dump_llvm_ir) std::fputs("\n", stdout);
    }

    const bool need_ir = opts.dump_llvm_ir
                      || opts.run || !opts.output.empty();
    std::string llvm_ir;
    if (need_ir) {
        llvm_ir = frappe::codegen::emit_llvm_ir(
            mod,
            frappe::codegen::EmitOptions{
                .module_name = opts.input,
                .opt_level   = opts.opt_level,
            },
            &engine);

        if (frappe::diagnostics::has_errors(&engine)) {
            frappe::diagnostics::print(&engine);
            return 1;
        }
    }

    if (opts.dump_llvm_ir) {
        std::fwrite(llvm_ir.data(), 1, llvm_ir.size(), stdout);
        std::fflush(stdout);
        return 0;
    }

    // --run or -o: compile to executable
    const bool temporary = opts.run;
    const std::string exe_path = temporary ? make_temp_exe_path() : opts.output;

    std::string clang_err;
    if (!frappe::codegen::compile_to_executable(llvm_ir, exe_path, &clang_err)) {
        std::fprintf(stderr, "codegen: %s\n", clang_err.c_str());
        return 1;
    }

    if (temporary) {
        int rc = run_executable(exe_path);
        std::remove(exe_path.c_str());
        std::remove((exe_path + ".ll").c_str());
        return rc;
    }

    if (!opts.dump_ast) {
        std::printf("wrote %s\n", exe_path.c_str());
    }
    return 0;
}