//
// Created by antar on 22/10/26.
//

#pragma  once
#include <string>


namespace frappe::mir {
    struct MirModule;
}

namespace frappe::diagnostics {
    struct Engine;
}

namespace frappe::codegen {
    enum class OptLevel {
        None, // no passes — raw IR straight from IRBuilder
        Less, // mem2reg + instcombine + simplifycfg + minor cleanups
        Default, // standard pipeline
        Aggressive, // standard + more aggressive inlining
    };


    struct EmitOptions {
        std::string_view module_name = "frappe_module";
        OptLevel opt_level = OptLevel::None;
    };

    [[nodiscard]] std::string emit_llvm_ir(
        const mir::MirModule &mir,
        EmitOptions opts,
        diagnostics::Engine *diag);

    // Path to the Frappe runtime static library, baked at build time via
    // FRAPPE_RUNTIME_LIB. Empty if the build didn't configure it.
    [[nodiscard]] std::string_view runtime_lib_path();

    // Write `llvm_ir` to `<exe_path>.ll`, invoke `clang` to link it against
    // the runtime, and produce an executable at `exe_path`.
    // Returns false and fills `error_out` on any failure.
    bool compile_to_executable(std::string_view llvm_ir,
                               std::string_view exe_path,
                               std::string *error_out);
}
