//
// Created by antar on 22/10/26.
//

#include "codegen_internal.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <vector>
#include <frappe/diagnostics/engine.hpp>
#include <frappe/fatal/fatality.hpp>


namespace frappe::codegen::detail {
    using mir::MirBinaryOp;
    using mir::MirUnaryOp;
    using mir::MirValue;

    namespace {
        template<typename... Ts>
        struct overloaded : Ts... {
            using Ts::operator()...;
        };

        template<typename... Ts>
        overloaded(Ts...) -> overloaded<Ts...>;

        [[nodiscard]] bool is_string_type(const types::Type *t) {
            return t && t->kind == types::TypeKind::Builtin
                   && static_cast<const types::BuiltinType *>(t)->b == resolver::Builtin::String;
        }

        [[nodiscard]] bool is_float_type(const types::Type *t) {
            if (!t || t->kind != types::TypeKind::Builtin) return false;
            using resolver::Builtin;
            auto b = static_cast<const types::BuiltinType *>(t)->b;
            return b == Builtin::Float || b == Builtin::Float32 || b == Builtin::Float64;
        }

        [[nodiscard]] bool is_bool_type(const types::Type *t) {
            return t && t->kind == types::TypeKind::Builtin
                   && static_cast<const types::BuiltinType *>(t)->b == resolver::Builtin::Bool;
        }

        [[nodiscard]] bool is_unsigned_int_type(const types::Type *t) {
            if (!t || t->kind != types::TypeKind::Builtin) return false;
            using resolver::Builtin;
            auto b = static_cast<const types::BuiltinType *>(t)->b;
            return b == Builtin::UInt8 || b == Builtin::UInt16
                   || b == Builtin::UInt32 || b == Builtin::UInt64;
        }

        [[nodiscard]] const types::Type *
        pick_operand_type(CodegenContext &ctx, const mir::MirBinary &x) {
            if (auto *t = mir_type_of_value(ctx, x.left)) return t;
            if (auto *t = mir_type_of_value(ctx, x.right)) return t;
            return mir_type_of_value(ctx, MirValue{x.destination});
        }

        void handle_store_local(CodegenContext &ctx, const mir::MirStoreLocal &x) {
            store_local(ctx, x.local, load_value(ctx, x.value));
        }

        void handle_move(CodegenContext &ctx, const mir::MirMove &x) {
            store_temp(ctx, x.destination, load_value(ctx, x.value));
        }

        void handle_load_const(CodegenContext &ctx, const mir::MirLoadConst &x) {
            store_temp(ctx, x.destination, materialize_constant(ctx, x.constant));
        }

        void handle_load_local(CodegenContext &ctx, const mir::MirLoadLocal &x) {
            auto *slot = ctx.state.local_slots[x.local.index];
            store_temp(ctx, x.destination,
                       ctx.builder.CreateLoad(slot->getAllocatedType(), slot, "ll"));
        }

        void handle_label(CodegenContext &ctx, const mir::MirLabelInstruction &x) {
            auto it = ctx.state.label_blocks.find(x.label.index);
            if (it == ctx.state.label_blocks.end())
                FRAPPE_UNREACHABLE({}, "codegen: unknown label {}", x.label.index);
            auto *current = ctx.builder.GetInsertBlock();
            if (current && !current->getTerminator())
                ctx.builder.CreateBr(it->second);
            ctx.builder.SetInsertPoint(it->second);
        }

        void handle_jump(CodegenContext &ctx, const mir::MirJump &x) {
            auto it = ctx.state.label_blocks.find(x.label.index);
            if (it == ctx.state.label_blocks.end())
                FRAPPE_UNREACHABLE({}, "codegen: unknown label {}", x.label.index);
            ctx.builder.CreateBr(it->second);
        }

        void handle_branch(CodegenContext &ctx, const mir::MirBranch &x) {
            auto *cond = load_value(ctx, x.condition);
            auto t_it = ctx.state.label_blocks.find(x.then_label.index);
            auto f_it = ctx.state.label_blocks.find(x.else_label.index);
            if (t_it == ctx.state.label_blocks.end() || f_it == ctx.state.label_blocks.end())
                FRAPPE_UNREACHABLE({}, "codegen: branch to unknown label");
            ctx.builder.CreateCondBr(cond, t_it->second, f_it->second);
        }

        void handle_binary(CodegenContext &ctx, const mir::MirBinary &x) {
            auto *lhs = load_value(ctx, x.left);
            auto *rhs = load_value(ctx, x.right);
            auto *elem_ty = pick_operand_type(ctx, x);

            llvm::Value *result = nullptr;

            if (is_string_type(elem_ty)) {
                if (x.op == MirBinaryOp::Add) {
                    result = ctx.builder.CreateCall(
                        ctx.rt_str_concat, {lhs, rhs}, "cat");
                } else if (x.op == MirBinaryOp::Eq ||
                           x.op == MirBinaryOp::Neq) {
                    auto *r = ctx.builder.CreateCall(
                        ctx.rt_str_eq, {lhs, rhs}, "seq");

                    auto *eq = ctx.builder.CreateICmpNE(
                        r, ctx.builder.getInt64(0), "seqeq");

                    result = x.op == MirBinaryOp::Eq
                                 ? eq
                                 : ctx.builder.CreateNot(eq, "sneq");
                } else {
                    FRAPPE_UNREACHABLE(
                        {}, "codegen: unsupported String binary op");
                }
            } else if (is_float_type(elem_ty)) {
                switch (x.op) {
                    case MirBinaryOp::Add: result = ctx.builder.CreateFAdd(lhs, rhs, "fadd");
                        break;
                    case MirBinaryOp::Sub: result = ctx.builder.CreateFSub(lhs, rhs, "fsub");
                        break;
                    case MirBinaryOp::Mul: result = ctx.builder.CreateFMul(lhs, rhs, "fmul");
                        break;
                    case MirBinaryOp::Div: result = ctx.builder.CreateFDiv(lhs, rhs, "fdiv");
                        break;
                    case MirBinaryOp::Mod: result = ctx.builder.CreateFRem(lhs, rhs, "frem");
                        break;
                    case MirBinaryOp::Eq: result = ctx.builder.CreateFCmpOEQ(lhs, rhs, "feq");
                        break;
                    case MirBinaryOp::Neq: result = ctx.builder.CreateFCmpONE(lhs, rhs, "fne");
                        break;
                    case MirBinaryOp::Lt: result = ctx.builder.CreateFCmpOLT(lhs, rhs, "flt");
                        break;
                    case MirBinaryOp::Lte: result = ctx.builder.CreateFCmpOLE(lhs, rhs, "fle");
                        break;
                    case MirBinaryOp::Gt: result = ctx.builder.CreateFCmpOGT(lhs, rhs, "fgt");
                        break;
                    case MirBinaryOp::Gte: result = ctx.builder.CreateFCmpOGE(lhs, rhs, "fge");
                        break;
                }
            } else if (is_bool_type(elem_ty)) {
                if (x.op == MirBinaryOp::Add) {
                    result = ctx.builder.CreateOr(lhs, rhs, "bor");
                } else if (x.op == MirBinaryOp::Mul) {
                    result = ctx.builder.CreateAnd(lhs, rhs, "band");
                } else if (x.op == MirBinaryOp::Eq ||
                           x.op == MirBinaryOp::Neq) {
                    result = x.op == MirBinaryOp::Eq
                                 ? ctx.builder.CreateICmpEQ(lhs, rhs, "beq")
                                 : ctx.builder.CreateICmpNE(lhs, rhs, "bne");
                } else {
                    FRAPPE_UNREACHABLE(
                        {}, "codegen: unsupported Bool binary op");
                }
            } else {
                const bool u = is_unsigned_int_type(elem_ty);
                switch (x.op) {
                    case MirBinaryOp::Add: result = ctx.builder.CreateAdd(lhs, rhs, "add");
                        break;
                    case MirBinaryOp::Sub: result = ctx.builder.CreateSub(lhs, rhs, "sub");
                        break;
                    case MirBinaryOp::Mul: result = ctx.builder.CreateMul(lhs, rhs, "mul");
                        break;
                    case MirBinaryOp::Div:
                        result = u
                                     ? ctx.builder.CreateUDiv(lhs, rhs, "udiv")
                                     : ctx.builder.CreateSDiv(lhs, rhs, "sdiv");
                        break;
                    case MirBinaryOp::Mod:
                        result = u
                                     ? ctx.builder.CreateURem(lhs, rhs, "urem")
                                     : ctx.builder.CreateSRem(lhs, rhs, "srem");
                        break;
                    case MirBinaryOp::Eq: result = ctx.builder.CreateICmpEQ(lhs, rhs, "eq");
                        break;
                    case MirBinaryOp::Neq: result = ctx.builder.CreateICmpNE(lhs, rhs, "ne");
                        break;
                    case MirBinaryOp::Lt:
                        result = u
                                     ? ctx.builder.CreateICmpULT(lhs, rhs, "ult")
                                     : ctx.builder.CreateICmpSLT(lhs, rhs, "slt");
                        break;
                    case MirBinaryOp::Lte:
                        result = u
                                     ? ctx.builder.CreateICmpULE(lhs, rhs, "ule")
                                     : ctx.builder.CreateICmpSLE(lhs, rhs, "sle");
                        break;
                    case MirBinaryOp::Gt:
                        result = u
                                     ? ctx.builder.CreateICmpUGT(lhs, rhs, "ugt")
                                     : ctx.builder.CreateICmpSGT(lhs, rhs, "sgt");
                        break;
                    case MirBinaryOp::Gte:
                        result = u
                                     ? ctx.builder.CreateICmpUGE(lhs, rhs, "uge")
                                     : ctx.builder.CreateICmpSGE(lhs, rhs, "sge");
                        break;
                }
            }

            store_temp(ctx, x.destination, result);
        }

        void handle_unary(CodegenContext &ctx, const mir::MirUnary &x) {
            auto *operand = load_value(ctx, x.operand);
            const types::Type *t = mir_type_of_value(ctx, x.operand);
            if (!t) t = mir_type_of_value(ctx, MirValue{x.destination});
            llvm::Value *result = nullptr;
            switch (x.op) {
                case MirUnaryOp::Neg:
                    result = is_float_type(t)
                                 ? ctx.builder.CreateFNeg(operand, "fneg")
                                 : ctx.builder.CreateNeg(operand, "neg");
                    break;
                case MirUnaryOp::Not:
                    result = ctx.builder.CreateNot(operand, "not");
                    break;
            }
            store_temp(ctx, x.destination, result);
        }

        void handle_virtual_call(CodegenContext &ctx, const mir::MirCall &x) {
            if (x.args.empty() || !x.vtable_slot) {
                diagnostics::report_error(ctx.diag, {},
                                          "codegen: virtual call missing receiver or vtable_slot");
                if (x.destination)
                    store_temp(ctx, *x.destination,
                               llvm::UndefValue::get(ctx.state.temp_slots[x.destination->index]
                                   ->getAllocatedType()));
                return;
            }
            // args[0] is the receiver.
            auto *receiver = load_value(ctx, x.args[0]);
            auto *vptr = ctx.builder.CreateLoad(ctx.builder.getPtrTy(), receiver, "vptr");
            auto *slot_ptr = ctx.builder.CreateInBoundsGEP(
                ctx.builder.getPtrTy(), vptr,
                ctx.builder.getInt64(*x.vtable_slot), "vslot");
            auto *fn_ptr = ctx.builder.CreateLoad(ctx.builder.getPtrTy(), slot_ptr, "vfn");

            // Build the function signature: receiver first, then actual args.
            std::vector<llvm::Type *> arg_types;
            std::vector<llvm::Value *> arg_values;
            arg_types.reserve(x.args.size());
            arg_values.reserve(x.args.size());
            for (auto &a: x.args) {
                arg_values.push_back(load_value(ctx, a));
                auto *t = mir_type_of_value(ctx, a);
                arg_types.push_back(t ? llvm_type(ctx, t) : ctx.builder.getPtrTy());
            }

            llvm::Type *ret_ty = ctx.builder.getVoidTy();
            if (x.destination) {
                auto &temps = ctx.state.mir_fn->temp_types;
                if (x.destination->index < temps.size() && temps[x.destination->index]) {
                    ret_ty = llvm_type(ctx, temps[x.destination->index]);
                }
            }

            auto *ft = llvm::FunctionType::get(ret_ty, arg_types, false);
            auto *call = ctx.builder.CreateCall(ft, fn_ptr, arg_values);
            if (x.destination) store_temp(ctx, *x.destination, call);
        }

        void handle_call(CodegenContext &ctx, const mir::MirCall &x) {
            if (x.is_virtual) {
                handle_virtual_call(ctx, x);
                return;
            }
            if (!std::holds_alternative<mir::MirDirectCallee>(x.callee))
                FRAPPE_UNREACHABLE({}, "codegen: indirect calls not supported in v1");

            auto &dc = std::get<mir::MirDirectCallee>(x.callee);
            auto *fn = find_function(ctx, dc.name);
            if (!fn) {
                diagnostics::report_error(ctx.diag, {},
                                          "codegen: undefined function '{}'", dc.name);
                if (x.destination)
                    store_temp(ctx, *x.destination,
                               llvm::UndefValue::get(ctx.state.temp_slots[x.destination->index]
                                   ->getAllocatedType()));
                return;
            }

            std::vector<llvm::Value *> args;
            args.reserve(x.args.size());
            for (auto &a: x.args) args.push_back(load_value(ctx, a));

            auto *call = ctx.builder.CreateCall(fn, args);
            if (x.destination) store_temp(ctx, *x.destination, call);
        }

        void handle_return(CodegenContext &ctx, const mir::MirReturn &x) {
            if (x.value) {
                ctx.builder.CreateRet(load_value(ctx, *x.value));
            } else if (ctx.state.llvm_fn->getReturnType()->isVoidTy()) {
                ctx.builder.CreateRetVoid();
            } else {
                // MIR guarantees a terminator per body; if we hit this, it is a MIR bug.
                ctx.builder.CreateUnreachable();
            }
        }

        void handle_panic(CodegenContext &ctx, const mir::MirPanic &x) {
            auto *msg = load_value(ctx, x.message);
            ctx.builder.CreateCall(ctx.rt_panic, {msg});
            ctx.builder.CreateUnreachable();
        }

        void handle_new_object(CodegenContext &ctx, const mir::MirNewObject &x) {
            auto *info = find_class_info(ctx, x.class_name);
            if (!info) {
                diagnostics::report_error(ctx.diag, {},
                                          "codegen: unknown class '{}'", x.class_name);
                store_temp(ctx, x.destination,
                           llvm::ConstantPointerNull::get(ctx.builder.getPtrTy()));
                return;
            }

            auto size = ctx.mod.getDataLayout().getTypeAllocSize(info->type);
            auto *mem = ctx.builder.CreateCall(
                ctx.rt_alloc,
                {ctx.builder.getInt64(static_cast<std::uint64_t>(size.getFixedValue()))},
                "obj");
            //  initialize the vptr at struct offset 0. Every class has
            // the field (layout uniformity), but only classes with interfaces have
            // an actual vtable global. Others get null — the checker prevents
            // virtual dispatch through them, so the null is inert.
            llvm::Constant *vtable_val;
            if (info->vtable_global) {
                vtable_val = info->vtable_global; // GlobalVariable* → Constant* implicit
            } else {
                vtable_val = llvm::ConstantPointerNull::get(ctx.builder.getPtrTy());
            }
            auto *vptr_slot = ctx.builder.CreateStructGEP(info->type, mem, 0, "vptr_slot");
            ctx.builder.CreateStore(vtable_val, vptr_slot);

            if (!info->init_fn_name.empty()) {
                if (auto *init_fn = find_function(ctx, info->init_fn_name)) {
                    std::vector<llvm::Value *> args;
                    args.reserve(1 + x.args.size());
                    args.push_back(mem);
                    for (auto &a: x.args) args.push_back(load_value(ctx, a));
                    ctx.builder.CreateCall(init_fn, args);
                }
            }
            store_temp(ctx, x.destination, mem);
        }

        void handle_build_array(CodegenContext &ctx, const mir::MirBuildArray &x) {
            auto *arr_ty = mir_type_of_value(ctx, MirValue{x.destination});
            if (!arr_ty || arr_ty->kind != types::TypeKind::Array)
                FRAPPE_UNREACHABLE({}, "codegen: MirBuildArray dest is not Array");

            auto *at = static_cast<const types::ArrayType *>(arr_ty);
            auto *elem_llvm_ty = llvm_type(ctx, at->element);
            auto elem_size = ctx.mod.getDataLayout().getTypeAllocSize(elem_llvm_ty);
            auto *arr = ctx.builder.CreateCall(ctx.rt_alloc_array, {
                                                   ctx.builder.getInt64(
                                                       elem_size.getFixedValue()),
                                                   ctx.builder.getInt64(x.elements.size()),
                                               }, "arr");

            for (std::size_t i = 0; i < x.elements.size(); ++i) {
                auto *val = load_value(ctx, x.elements[i]);
                auto *gep = ctx.builder.CreateInBoundsGEP(
                    elem_llvm_ty, arr, ctx.builder.getInt64(i));
                ctx.builder.CreateStore(val, gep);
            }
            store_temp(ctx, x.destination, arr);
        }

        [[nodiscard]] ClassInfo *class_of_operand(CodegenContext &ctx, MirValue v,
                                                  std::string_view what) {
            auto *t = mir_type_of_value(ctx, v);
            if (!t || t->kind != types::TypeKind::Named)
                FRAPPE_UNREACHABLE({}, "codegen: {} on non-named type", what);
            auto *nt = static_cast<const types::NamedType *>(t);
            if (nt->decl.kind != types::NamedDeclKind::Class)
                FRAPPE_UNREACHABLE({}, "codegen: {} on non-class type", what);
            return find_class_info(ctx, nt->decl.class_->name.text);
        }

        void handle_get_field(CodegenContext &ctx, const mir::MirGetField &x) {
            auto *info = class_of_operand(ctx, x.object, "GetField");
            auto it = info->field_index.find(x.field_name);
            if (it == info->field_index.end())
                FRAPPE_UNREACHABLE({}, "codegen: unknown field '{}'", x.field_name);

            auto *obj = load_value(ctx, x.object);
            auto *gep = ctx.builder.CreateStructGEP(info->type, obj, it->second, "fp");
            auto *val = ctx.builder.CreateLoad(info->type->getElementType(it->second), gep, "fv");
            store_temp(ctx, x.destination, val);
        }


        void handle_set_field(CodegenContext &ctx, const mir::MirSetField &x) {
            auto *info = class_of_operand(ctx, x.object, "SetField");
            auto it = info->field_index.find(x.field_name);
            if (it == info->field_index.end())
                FRAPPE_UNREACHABLE({}, "codegen: unknown field '{}'", x.field_name);

            auto *obj = load_value(ctx, x.object);
            auto *val = load_value(ctx, x.value);
            auto *gep = ctx.builder.CreateStructGEP(info->type, obj, it->second);
            ctx.builder.CreateStore(val, gep);
        }

        void handle_get_index(CodegenContext &ctx, const mir::MirGetIndex &x) {
            auto *elem_ty = mir_type_of_value(ctx, MirValue{x.destination});
            if (!elem_ty)
                FRAPPE_UNREACHABLE({}, "codegen: GetIndex dest has no type");

            auto *elem_llvm = llvm_type(ctx, elem_ty);
            auto *obj = load_value(ctx, x.object);
            auto *idx = load_value(ctx, x.index);
            auto *gep = ctx.builder.CreateInBoundsGEP(elem_llvm, obj, idx, "ep");
            auto *val = ctx.builder.CreateLoad(elem_llvm, gep, "ev");
            store_temp(ctx, x.destination, val);
        }


        void handle_set_index(CodegenContext &ctx, const mir::MirSetIndex &x) {
            auto *obj_ty = mir_type_of_value(ctx, x.object);
            if (!obj_ty || obj_ty->kind != types::TypeKind::Array)
                FRAPPE_UNREACHABLE({}, "codegen: SetIndex on non-array");

            auto *at = static_cast<const types::ArrayType *>(obj_ty);
            auto *elem_llvm = llvm_type(ctx, at->element);
            auto *obj = load_value(ctx, x.object);
            auto *idx = load_value(ctx, x.index);
            auto *val = load_value(ctx, x.value);
            auto *gep = ctx.builder.CreateInBoundsGEP(elem_llvm, obj, idx);
            ctx.builder.CreateStore(val, gep);
        }
    }

    void emit_instruction(CodegenContext &ctx, const mir::MirInstruction &inst) {
        std::visit(overloaded{
                       [&](const mir::MirStoreLocal &x) { handle_store_local(ctx, x); },
                       [&](const mir::MirMove &x) { handle_move(ctx, x); },
                       [&](const mir::MirLoadConst &x) { handle_load_const(ctx, x); },
                       [&](const mir::MirLoadLocal &x) { handle_load_local(ctx, x); },
                       [&](const mir::MirBinary &x) { handle_binary(ctx, x); },
                       [&](const mir::MirUnary &x) { handle_unary(ctx, x); },
                       [&](const mir::MirLabelInstruction &x) { handle_label(ctx, x); },
                       [&](const mir::MirJump &x) { handle_jump(ctx, x); },
                       [&](const mir::MirBranch &x) { handle_branch(ctx, x); },
                       [&](const mir::MirCall &x) { handle_call(ctx, x); },
                       [&](const mir::MirReturn &x) { handle_return(ctx, x); },
                       [&](const mir::MirPanic &x) { handle_panic(ctx, x); },
                       [&](const mir::MirNewObject &x) { handle_new_object(ctx, x); },
                       [&](const mir::MirBuildArray &x) { handle_build_array(ctx, x); },
                       [&](const mir::MirGetField &x) { handle_get_field(ctx, x); },
                       [&](const mir::MirSetField &x) { handle_set_field(ctx, x); },
                       [&](const mir::MirGetIndex &x) { handle_get_index(ctx, x); },
                       [&](const mir::MirSetIndex &x) { handle_set_index(ctx, x); },
                   }, inst);
    }

    void emit_function(CodegenContext &ctx, const mir::MirFunction &mfn, llvm::Function *fn) {
        std::unordered_map<std::uint32_t, llvm::BasicBlock *> labels;
        for (auto &inst: mfn.instructions) {
            if (auto *lbl = std::get_if<mir::MirLabelInstruction>(&inst)) {
                if (!labels.contains(lbl->label.index)) {
                    labels[lbl->label.index] = llvm::BasicBlock::Create(
                        ctx.llvm, "L" + std::to_string(lbl->label.index), fn);
                }
            }
        }

        auto *entry = llvm::BasicBlock::Create(ctx.llvm, "entry", fn);
        ctx.builder.SetInsertPoint(entry);

        auto saved_state = std::move(ctx.state);
        ctx.state = CodegenContext::FnState{};
        ctx.state.mir_fn = &mfn;
        ctx.state.llvm_fn = fn;
        ctx.state.label_blocks = std::move(labels);

        // Locals
        ctx.state.local_slots.resize(mfn.locals.size());
        for (std::size_t i = 0; i < mfn.locals.size(); ++i) {
            std::string name = mfn.locals[i].name.empty()
                                   ? "v"
                                   : std::string(mfn.locals[i].name);
            ctx.state.local_slots[i] = ctx.builder.CreateAlloca(
                llvm_type(ctx, mfn.locals[i].type), nullptr, name);
        }
        for (std::size_t i = 0; i < mfn.parameters.size(); ++i) {
            ctx.builder.CreateStore(fn->getArg(static_cast<unsigned>(i)),
                                    ctx.state.local_slots[mfn.parameters[i].index]);
        }

        // Temps
        ctx.state.temp_slots.resize(mfn.temp_types.size());
        for (std::size_t i = 0; i < mfn.temp_types.size(); ++i) {
            ctx.state.temp_slots[i] = ctx.builder.CreateAlloca(
                llvm_type(ctx, mfn.temp_types[i]), nullptr, "t" + std::to_string(i));
        }

        // Body
        for (auto &inst: mfn.instructions) emit_instruction(ctx, inst);

        // Guarantee terminator
        auto *cur = ctx.builder.GetInsertBlock();
        if (cur && !cur->getTerminator()) {
            if (fn->getReturnType()->isVoidTy()) ctx.builder.CreateRetVoid();
            else ctx.builder.CreateUnreachable();
        }

        ctx.state = std::move(saved_state);
    }
}
