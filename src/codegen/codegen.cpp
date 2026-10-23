//
// Created by antar on 22/10/26.
//
#include <frappe/codegen/codegen.hpp>
#include "codegen_internal.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/OptimizationLevel.h>

#include <cstring>
#include <type_traits>

#include <frappe/diagnostics/engine.hpp>
#include <frappe/fatal/fatality.hpp>


namespace frappe::codegen::detail {
    using mir::MirConstantId;
    using mir::MirLocalId;
    using mir::MirTemp;
    using mir::MirValue;

    namespace {
        [[nodiscard]] llvm::StructType *string_struct_type(llvm::LLVMContext &c) {
            return llvm::StructType::get(c, {
                                             llvm::PointerType::getUnqual(c),
                                             llvm::Type::getInt64Ty(c),
                                         });
        }

        [[nodiscard]] llvm::StringRef to_ref(std::string_view s) {
            return {s.data(), s.size()};
        }
    }

    void initialize_llvm_targets() {
        static bool done = false;
        if (done) return;
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        done = true;
    }

    // Build a TargetMachine for the host triple, or report a diagnostic
    // and return nullptr.
    std::unique_ptr<llvm::TargetMachine>
    make_host_target_machine(diagnostics::Engine *diag) {
        initialize_llvm_targets();
        llvm::Triple triple{llvm::sys::getDefaultTargetTriple()};
        std::string err;
        const llvm::Target *target =
                llvm::TargetRegistry::lookupTarget(triple, err);
        if (!target) {
            diagnostics::report_error(diag, {},
                                      "codegen: no LLVM target for '{}': {}", triple.str(), err);
            return nullptr;
        }
        llvm::TargetOptions opts;
        // PIC is the safe default for a compiler that shells out to clang;
        // it also keeps the IR usable in shared libraries later.
        std::optional<llvm::Reloc::Model> reloc = llvm::Reloc::PIC_;

        std::unique_ptr<llvm::TargetMachine> tm{
            target->createTargetMachine(
                triple, // const Triple&
                "generic", // CPU — portable baseline, no ISA extensions
                "", // Features — let the CPU decide
                opts, // TargetOptions
                reloc // Reloc::Model
            )
        };
        if (!tm) {
            diagnostics::report_error(diag, {},
                                      "codegen: failed to create TargetMachine for '{}'", triple.str());
            return nullptr;
        }
        return tm;
    }

    llvm::OptimizationLevel to_llvm_opt(OptLevel lvl) {
        switch (lvl) {
            case OptLevel::None: return llvm::OptimizationLevel::O0;
            case OptLevel::Less: return llvm::OptimizationLevel::O1;
            case OptLevel::Default: return llvm::OptimizationLevel::O2;
            case OptLevel::Aggressive: return llvm::OptimizationLevel::O3;
        }
        return llvm::OptimizationLevel::O0;
    }

    // Run the new-pass-manager pipeline in place. Returns false if the module
    // fails verification *after* the passes run — that's a codegen bug, not
    // a pass bug, and it needs to surface immediately.
    bool run_optimization_passes(CodegenContext &ctx, OptLevel lvl) {
        if (lvl == OptLevel::None) return true;
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;
        llvm::PassBuilder pb;
        pb.registerModuleAnalyses(MAM);
        pb.registerCGSCCAnalyses(CGAM);
        pb.registerFunctionAnalyses(FAM);
        pb.registerLoopAnalyses(LAM);
        pb.crossRegisterProxies(LAM, FAM, CGAM, MAM);
        auto mpm = pb.buildPerModuleDefaultPipeline(to_llvm_opt(lvl));
        mpm.run(ctx.mod, MAM);

        std::string verify_err;
        llvm::raw_string_ostream verify_os(verify_err);
        if (llvm::verifyModule(ctx.mod, &verify_os)) {
            diagnostics::report_error(ctx.diag, {},
                                      "codegen: IR invalid after optimization — this is a codegen bug "
                                      "exposed by the pass pipeline, not a pass bug:\n{}", verify_err);
            return false;
        }
        return true;
    }

    llvm::Type *llvm_type(CodegenContext &ctx, const types::Type *t) {
        if (!t) return ctx.builder.getVoidTy();
        if (auto it = ctx.type_cache.find(t); it != ctx.type_cache.end()) return it->second;
        using types::TypeKind;
        using types::BuiltinType;
        using types::TupleType;
        llvm::Type *result = nullptr;
        switch (t->kind) {
            case TypeKind::Builtin: {
                using resolver::Builtin;
                switch (static_cast<const BuiltinType *>(t)->b) {
                    case Builtin::Int:
                    case Builtin::Int64:
                    case Builtin::UInt64: result = ctx.builder.getInt64Ty();
                        break;
                    case Builtin::Int32:
                    case Builtin::UInt32: result = ctx.builder.getInt32Ty();
                        break;
                    case Builtin::Int16:
                    case Builtin::UInt16: result = ctx.builder.getInt16Ty();
                        break;
                    case Builtin::Int8:
                    case Builtin::UInt8:
                    case Builtin::Char: result = ctx.builder.getInt8Ty();
                        break;
                    case Builtin::Float:
                    case Builtin::Float64: result = ctx.builder.getDoubleTy();
                        break;
                    case Builtin::Float32: result = ctx.builder.getFloatTy();
                        break;
                    case Builtin::Bool: result = ctx.builder.getInt1Ty();
                        break;
                    case Builtin::String: result = string_struct_type(ctx.llvm);
                        break;
                    case Builtin::Nil: result = ctx.builder.getPtrTy();
                        break;
                    case Builtin::Void: result = ctx.builder.getVoidTy();
                        break;
                }
                break;
            }
            case TypeKind::Named:
            case TypeKind::Array:
            case TypeKind::Optional:
            case TypeKind::Func:
            case TypeKind::Error:
            case TypeKind::Unknown:
            case TypeKind::TypeVar:
                result = ctx.builder.getPtrTy();
                break;
            case TypeKind::Tuple: {
                auto *tt = static_cast<const TupleType *>(t);
                std::vector<llvm::Type *> elements;
                elements.reserve(tt->elems.size());
                for (auto *e: tt->elems) elements.push_back(llvm_type(ctx, e));
                result = llvm::StructType::get(ctx.llvm, elements);
                break;
            }
        }
        if (!result) result = ctx.builder.getPtrTy();
        ctx.type_cache[t] = result;
        return result;
    }

    const types::Type *mir_type_of_value(CodegenContext &ctx, MirValue v) {
        if (!ctx.state.mir_fn) return nullptr;
        if (auto *t = std::get_if<MirTemp>(&v)) {
            auto &temps = ctx.state.mir_fn->temp_types;
            if (t->index < temps.size()) return temps[t->index];
        }
        if (auto *l = std::get_if<MirLocalId>(&v)) {
            auto &locals = ctx.state.mir_fn->locals;
            if (l->index < locals.size()) return locals[l->index].type;
        }
        return nullptr;
    }

    llvm::Value *materialize_constant(CodegenContext &ctx, MirConstantId id) {
        auto &constant = ctx.state.mir_fn->constants[id.index];
        return std::visit([&](auto &&c) -> llvm::Value * {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, std::int64_t>) {
                return ctx.builder.getInt64(c);
            } else if constexpr (std::is_same_v<T, double>) {
                return llvm::ConstantFP::get(ctx.builder.getDoubleTy(), c);
            } else if constexpr (std::is_same_v<T, bool>) {
                return ctx.builder.getInt1(c);
            } else if constexpr (std::is_same_v<T, std::string_view>) {
                std::string key{c};
                llvm::GlobalVariable *gv = nullptr;
                if (auto it = ctx.string_globals.find(key); it != ctx.string_globals.end()) {
                    gv = it->second;
                } else {
                    gv = ctx.builder.CreateGlobalString(c, ".str");
                    ctx.string_globals.emplace(std::move(key), gv);
                }
                auto *str_ty = string_struct_type(ctx.llvm);
                return llvm::ConstantStruct::get(str_ty,
                                                 {gv, ctx.builder.getInt64(static_cast<std::uint64_t>(c.size()))});
            } else if constexpr (std::is_same_v<T, mir::MirNil>) {
                return llvm::ConstantPointerNull::get(ctx.builder.getPtrTy());
            } else {
                FRAPPE_UNREACHABLE({}, "codegen: unknown MirConstant variant");
            }
        }, constant);
    }

    llvm::Value *load_value(CodegenContext &ctx, MirValue v) {
        if (auto *t = std::get_if<MirTemp>(&v)) {
            auto *slot = ctx.state.temp_slots[t->index];
            return ctx.builder.CreateLoad(slot->getAllocatedType(), slot, "t");
        }
        if (auto *l = std::get_if<MirLocalId>(&v)) {
            auto *slot = ctx.state.local_slots[l->index];
            return ctx.builder.CreateLoad(slot->getAllocatedType(), slot, "l");
        }
        if (auto *c = std::get_if<MirConstantId>(&v)) {
            return materialize_constant(ctx, *c);
        }
        FRAPPE_UNREACHABLE({}, "codegen: bad MirValue");
    }

    void store_temp(CodegenContext &ctx, MirTemp t, llvm::Value *v) {
        ctx.builder.CreateStore(v, ctx.state.temp_slots[t.index]);
    }

    void store_local(CodegenContext &ctx, MirLocalId l, llvm::Value *v) {
        ctx.builder.CreateStore(v, ctx.state.local_slots[l.index]);
    }

    ClassInfo *find_class_info(CodegenContext &ctx, std::string_view name) {
        if (auto it = ctx.classes.find(name); it != ctx.classes.end()) return &it->second;
        return nullptr;
    }

    llvm::Function *find_function(CodegenContext &ctx, std::string_view name) {
        if (auto it = ctx.functions.find(name); it != ctx.functions.end()) return it->second;
        return nullptr;
    }

    void emit_runtime_decls(CodegenContext &ctx) {
        auto *i64 = ctx.builder.getInt64Ty();
        auto *ptr = ctx.builder.getPtrTy();
        auto *void_ty = ctx.builder.getVoidTy();
        auto *str_ty = string_struct_type(ctx.llvm);

        auto make_fn = [&](llvm::StringRef name, llvm::FunctionType *ft) {
            return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, &ctx.mod);
        };

        ctx.rt_alloc = make_fn("__frappe_alloc", llvm::FunctionType::get(ptr, {i64}, false));
        ctx.rt_alloc_array = make_fn("__frappe_alloc_array", llvm::FunctionType::get(ptr, {i64, i64}, false));
        ctx.rt_panic = make_fn("__frappe_panic", llvm::FunctionType::get(void_ty, {str_ty}, false));
        ctx.rt_panic->addFnAttr(llvm::Attribute::NoReturn);
        ctx.rt_str_concat = make_fn("__frappe_str_concat", llvm::FunctionType::get(str_ty, {str_ty, str_ty}, false));
        ctx.rt_str_eq = make_fn("__frappe_str_eq", llvm::FunctionType::get(i64, {str_ty, str_ty}, false));
    }

    void emit_classes(CodegenContext &ctx) {
        // Pass 1: opaque struct types so any `llvm_type(Named) -> ptr` works
        // during pass 2 body-filling.
        for (auto &mc: ctx.mir->classes) {
            auto *st = llvm::StructType::create(ctx.llvm, to_ref(mc.name));
            ClassInfo info{.type = st, .mir_class = &mc};
            ctx.classes.emplace(mc.name, std::move(info));
        }

        // Pass 2: fill in fields
        for (auto &mc: ctx.mir->classes) {
            auto *info = find_class_info(ctx, mc.name);
            std::vector<llvm::Type *> field_tys;
            field_tys.reserve(mc.fields.size());
            for (auto &f: mc.fields) {
                field_tys.push_back(llvm_type(ctx, f.type));
                info->field_index[f.name] = f.index;
            }
            info->type->setBody(field_tys, /*isPacked=*/false);

            for (auto &m: mc.methods) {
                if (m.name == "init") {
                    info->init_fn_name = m.fn_name;
                    break;
                }
            }
        }
    }

    void declare_functions(CodegenContext &ctx) {
        for (auto &mfn: ctx.mir->functions) {
            std::vector<llvm::Type *> param_tys;
            param_tys.reserve(mfn.parameters.size());
            for (auto p: mfn.parameters)
                param_tys.push_back(llvm_type(ctx, mfn.locals[p.index].type));

            auto *ret_ty = mfn.return_type
                               ? llvm_type(ctx, mfn.return_type)
                               : ctx.builder.getVoidTy();
            auto *ft = llvm::FunctionType::get(ret_ty, param_tys, false);
            auto *fn = llvm::Function::Create(
                ft, llvm::Function::ExternalLinkage, to_ref(mfn.name), &ctx.mod);

            for (std::size_t i = 0; i < mfn.parameters.size(); ++i) {
                auto &local = mfn.locals[mfn.parameters[i].index];
                if (!local.name.empty())
                    fn->getArg(static_cast<unsigned>(i))->setName(to_ref(local.name));
            }
            ctx.functions.emplace(std::string_view(fn->getName().data(), fn->getName().size()), fn);
        }
    }

    void emit_main_wrapper(CodegenContext &ctx) {
        auto it = ctx.functions.find("main");
        if (it == ctx.functions.end()) return;
        auto *user_main = it->second;

        user_main->setName("__frappe_user_main");
        ctx.functions.erase(it);
        ctx.functions.emplace("__frappe_user_main", user_main);

        auto *i32 = ctx.builder.getInt32Ty();
        auto *wrapper = llvm::Function::Create(
            llvm::FunctionType::get(i32, {}, false),
            llvm::Function::ExternalLinkage, "main", &ctx.mod);
        ctx.functions.emplace("main", wrapper);

        auto *entry = llvm::BasicBlock::Create(ctx.llvm, "entry", wrapper);
        ctx.builder.SetInsertPoint(entry);

        auto *call = ctx.builder.CreateCall(user_main, {});
        if (user_main->getReturnType()->isVoidTy()) {
            ctx.builder.CreateRet(ctx.builder.getInt32(0));
        } else if (call->getType() == i32) {
            ctx.builder.CreateRet(call);
        } else {
            auto *t = ctx.builder.CreateTrunc(call, i32);
            ctx.builder.CreateRet(t);
        }
    }
}

namespace frappe::codegen {
    using namespace detail;

    std::string emit_llvm_ir(const mir::MirModule &mir, EmitOptions opts, diagnostics::Engine *diag) {
#ifdef FRAPPE_LLVM_ENABLED
        auto tm = detail::make_host_target_machine(diag);
        if (!tm) return {};

        llvm::LLVMContext llvm_ctx;
        auto mod = std::make_unique<llvm::Module>(
            std::string(opts.module_name), llvm_ctx);
        const llvm::Triple triple{llvm::sys::getDefaultTargetTriple()};

        mod->setTargetTriple(triple);
        mod->setDataLayout(tm->createDataLayout());

        CodegenContext ctx{
            .llvm = llvm_ctx,
            .mod = *mod,
            .builder = llvm::IRBuilder<>(llvm_ctx),
            .diag = diag,
            .mir = &mir,
        };

        emit_runtime_decls(ctx);
        emit_classes(ctx);
        declare_functions(ctx);

        for (auto &mfn: mir.functions) {
            auto *fn = find_function(ctx, mfn.name);
            if (!fn) {
                diagnostics::report_error(diag, {},
                                          "codegen: function '{}' has no declaration", mfn.name);
                continue;
            }
            emit_function(ctx, mfn, fn);
        }

        emit_main_wrapper(ctx);

        {
            std::string verify_err;
            llvm::raw_string_ostream verify_os(verify_err);
            if (llvm::verifyModule(*mod, &verify_os)) {
                diagnostics::report_error(diag, {},
                                          "codegen: LLVM module verification failed:\n{}", verify_err);
                return {};
            }
        }
        if (!run_optimization_passes(ctx, opts.opt_level)) {
            return {};
        }

        std::string ir;
        llvm::raw_string_ostream os(ir);
        mod->print(os, nullptr);
        return ir;
#else
        diagnostics::report_error(diag, {},
                                  "codegen requires building with -DFRAPPE_LLVM=ON");
        return {};
#endif
    }
}
