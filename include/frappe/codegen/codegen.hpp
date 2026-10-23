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
}
