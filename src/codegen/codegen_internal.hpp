#pragma once

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <frappe/mir/instruction.hpp>
#include <frappe/mir/module.hpp>
#include <frappe/mir/value.hpp>
#include <frappe/types/types.hpp>

namespace frappe::diagnostics {
    struct Engine;
}

namespace frappe::codegen::detail {
    struct ClassInfo {
        llvm::StructType *type = nullptr;
        const mir::MirClass *mir_class = nullptr;
        std::unordered_map<std::string_view, std::uint32_t> field_index{};
        std::string_view init_fn_name{};
        llvm::GlobalVariable *vtable_global = nullptr;
    };

    struct CodegenContext {
        llvm::LLVMContext &llvm;
        llvm::Module &mod;
        llvm::IRBuilder<> builder;
        diagnostics::Engine *diag = nullptr;

        const mir::MirModule *mir = nullptr;

        std::unordered_map<std::string_view, ClassInfo> classes{};
        std::unordered_map<std::string_view, llvm::Function *> functions{};
        std::unordered_map<std::string, llvm::GlobalVariable *> string_globals{};
        std::unordered_map<const types::Type *, llvm::Type *> type_cache{};

        llvm::Function *rt_alloc = nullptr;
        llvm::Function *rt_alloc_array = nullptr;
        llvm::Function *rt_panic = nullptr;
        llvm::Function *rt_str_concat = nullptr;
        llvm::Function *rt_str_eq = nullptr;
        llvm::Function *rt_println_int = nullptr;
        llvm::Function *rt_println_float = nullptr;
        llvm::Function *rt_println_str = nullptr;
        llvm::Function *rt_bounds_error = nullptr;

        struct FnState {
            const mir::MirFunction *mir_fn = nullptr;
            llvm::Function *llvm_fn = nullptr;
            std::vector<llvm::AllocaInst *> local_slots{};
            std::vector<llvm::AllocaInst *> temp_slots{};
            std::unordered_map<std::uint32_t, llvm::BasicBlock *> label_blocks{};
        };

        FnState state{};
    };

    // ... function decls unchanged
    llvm::Type *llvm_type(CodegenContext &, const types::Type *);

    const types::Type *mir_type_of_value(CodegenContext &, mir::MirValue);

    llvm::Value *load_value(CodegenContext &, mir::MirValue);

    llvm::Value *materialize_constant(CodegenContext &, mir::MirConstantId);

    void store_temp(CodegenContext &, mir::MirTemp, llvm::Value *);

    void store_local(CodegenContext &, mir::MirLocalId, llvm::Value *);

    ClassInfo *find_class_info(CodegenContext &, std::string_view);

    llvm::Function *find_function(CodegenContext &, std::string_view);

    void emit_runtime_decls(CodegenContext &);

    void emit_classes(CodegenContext &);

    void declare_functions(CodegenContext &);

    void emit_main_wrapper(CodegenContext &);

    void emit_function(CodegenContext &, const mir::MirFunction &, llvm::Function *);

    void emit_instruction(CodegenContext &, const mir::MirInstruction &);

    void emit_vtables(CodegenContext &);
    [[nodiscard]] llvm::StructType *string_struct_type(llvm::LLVMContext &c);

} // namespace frappe::codegen::detail
