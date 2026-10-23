//
// Created by antar on 21/10/26.
//

#include <frappe/mir/builder.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <unordered_map>
#include <unordered_set>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>
#include <frappe/source/span.hpp>
#include <frappe/syntax/constants.hpp>
#include <frappe/fatal/fatality.hpp>


namespace frappe::mir {
    namespace {
        using checker::Checked_Fn;
        using checker::Checker;
        using types::Type;
        using types::TypeKind;

        enum class LowerResult {
            FallsThrough,
            Terminated,
        };

        enum class Color : std::uint8_t {
            White,
            Gray,
            Black,
        };

        [[nodiscard]] std::string_view intern_string(MirBuilder *b, std::string_view s) {
            if (s.empty()) return s;
            char *buf = static_cast<char *>(b->mr->allocate(s.size(), alignof(char)));
            std::memcpy(buf, s.data(), s.size());
            return std::string_view{buf, s.size()};
        }

        [[nodiscard]] const Type *type_of(MirBuilder *b, const void *node) {
            if (!node) return nullptr;
            if (b->current_inst) {
                if (auto it = b->current_inst->node_types.find(node); it != b->current_inst->node_types.end())
                    return it
                            ->second;
            }
            auto it = b->checker->node_types.find(node);
            return it != b->checker->node_types.end() ? it->second : nullptr;
        }

        [[nodiscard]] const Type *resolve_type_node(MirBuilder *b, const ast::Type *t) {
            if (!t) return b->checker->store.void_type;
            if (auto *cached = type_of(b, t)) return cached;
            return checker::resolve_type(b->checker, t);
        }

        [[nodiscard]] std::string_view call_name_for(MirBuilder *b, const void *node) {
            if (b->current_inst) {
                if (auto it = b->current_inst->call_names.find(node);
                    it != b->current_inst->call_names.end()) {
                    return it->second;
                }
            }
            if (auto it = b->checker->call_names.find(node);
                it != b->checker->call_names.end()) {
                return it->second;
            }
            return {};
        }

        [[nodiscard]] MirTemp new_temp(MirBuilder *b, const Type *t) {
            MirTemp temp{.index = b->next_temp++};
            b->current_fn->temp_types.push_back(t);
            return temp;
        }

        [[nodiscard]] MirLabel new_label(MirBuilder *b) {
            return {.index = b->next_label++};
        }

        template<typename Inst>
        void emit(MirBuilder *b, Inst inst) {
            b->current_fn->instructions.emplace_back(std::move(inst));
        }

        void emit_deferred(MirBuilder *b, std::pmr::vector<Defer_Block> &blocks) {
            // Reverse the *blocks* (LIFO), but emit each block's instructions forward.
            for (auto bit = blocks.rbegin(); bit != blocks.rend(); ++bit) {
                for (auto &inst: bit->instructions) {
                    b->current_fn->instructions.push_back(std::move(inst));
                }
            }
            blocks.clear();
        }

        void emit_all_deferred(MirBuilder *b) {
            for (auto it = b->defer_stack.rbegin(); it != b->defer_stack.rend(); ++it) {
                for (auto &bit: std::views::reverse(*it)) {
                    for (const auto &inst: bit.instructions) {
                        b->current_fn->instructions.push_back(inst); // copy
                    }
                }
            }
        }

        [[nodiscard]] MirConstantId add_constant(MirBuilder *b, MirConstant c) {
            MirConstantId id{.index = static_cast<uint32_t>(b->current_fn->constants.size())};
            b->current_fn->constants.push_back(std::move(c));
            return id;
        }

        [[nodiscard]] MirValue nil_value(MirBuilder *b) {
            return MirValue{add_constant(b, MirConstant{MirNil{}})};
        }

        [[nodiscard]] MirLocalId add_local(MirBuilder *b, std::string_view name, const Type *t) {
            MirLocalId id{.index = static_cast<uint32_t>(b->current_fn->locals.size())};
            b->current_fn->locals.push_back(MirLocal{id, name, t});
            return id;
        }

        [[nodiscard]] constexpr MirBinaryOp map_bin(ast::BinaryOp op) {
            switch (op) {
                case ast::BinaryOp::Add: return MirBinaryOp::Add;
                case ast::BinaryOp::Sub: return MirBinaryOp::Sub;
                case ast::BinaryOp::Mul: return MirBinaryOp::Mul;
                case ast::BinaryOp::Div: return MirBinaryOp::Div;
                case ast::BinaryOp::Mod: return MirBinaryOp::Mod;
                case ast::BinaryOp::Eq: return MirBinaryOp::Eq;
                case ast::BinaryOp::Neq: return MirBinaryOp::Neq;
                case ast::BinaryOp::Lt: return MirBinaryOp::Lt;
                case ast::BinaryOp::Lte: return MirBinaryOp::Lte;
                case ast::BinaryOp::Gt: return MirBinaryOp::Gt;
                case ast::BinaryOp::Gte: return MirBinaryOp::Gte;
                case ast::BinaryOp::And:
                case ast::BinaryOp::Or:
                    break;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr MirUnaryOp map_un(ast::UnaryOp op) {
            switch (op) {
                case ast::UnaryOp::Neg: return MirUnaryOp::Neg;
                case ast::UnaryOp::Not: return MirUnaryOp::Not;
            }
            std::unreachable();
        }

        LowerResult lower_stmt(MirBuilder *b, const ast::Stmt *s);

        LowerResult lower_block(MirBuilder *b, const ast::BlockStmt *blk);

        [[nodiscard]] MirValue lower_expr(MirBuilder *b, const ast::Expr *e);

        LowerResult lower_stmt(MirBuilder *b, const ast::Stmt *s) {
            using ast::StmtKind;
            if (!s) return LowerResult::FallsThrough;
            switch (s->kind) {
                case StmtKind::Expr: {
                    auto *es = static_cast<const ast::ExprStmt *>(s);
                    static_cast<void>(lower_expr(b, es->expr));
                    return LowerResult::FallsThrough;
                }
                case StmtKind::Var: {
                    auto *vs = static_cast<const ast::VarStmt *>(s);
                    auto *sym = resolver::lookup(b->checker->resolver_, vs);
                    const Type *ty = type_of(b, vs);
                    if (!ty && vs->type_) ty = resolve_type_node(b, vs->type_);
                    if (!ty) ty = b->checker->store.unknown_type;
                    MirValue init = lower_expr(b, vs->init);
                    auto local = add_local(b, vs->name.text, ty);
                    if (sym) b->scope[sym] = local;
                    emit(b, MirStoreLocal{local, init});
                    return LowerResult::FallsThrough;
                }
                case StmtKind::Block:
                    return lower_block(b, static_cast<const ast::BlockStmt *>(s));
                case StmtKind::If: {
                    auto *is = static_cast<const ast::IfStmt *>(s);
                    MirValue cond = lower_expr(b, is->condition);
                    auto then_l = new_label(b);
                    auto else_l = new_label(b);
                    auto end_l = new_label(b);
                    emit(b, MirBranch{cond, then_l, else_l});

                    emit(b, MirLabelInstruction{then_l});
                    auto then_result = lower_block(b, is->then_branch);
                    if (then_result == LowerResult::FallsThrough) {
                        emit(b, MirJump{end_l});
                    }

                    emit(b, MirLabelInstruction{else_l});
                    auto else_result = LowerResult::FallsThrough;
                    if (is->else_branch) {
                        else_result = lower_stmt(b, is->else_branch);
                        if (else_result == LowerResult::FallsThrough) {
                            emit(b, MirJump{end_l});
                        }
                    } else {
                        emit(b, MirJump{end_l});
                    }

                    // Both branches terminated — no fall-through path exists.
                    // Don't emit Lend at all; nothing jumps to it, and an orphan label
                    // would fail LLVM verification (blocks need terminators).
                    if (then_result == LowerResult::Terminated
                        && else_result == LowerResult::Terminated) {
                        return LowerResult::Terminated;
                    }

                    emit(b, MirLabelInstruction{end_l});
                    return LowerResult::FallsThrough;
                }
                case StmtKind::While: {
                    auto *ws = static_cast<const ast::WhileStmt *>(s);
                    auto top = new_label(b);
                    auto body = new_label(b);
                    auto end = new_label(b);
                    emit(b, MirLabelInstruction{top});
                    MirValue cond = lower_expr(b, ws->condition);
                    emit(b, MirBranch{cond, body, end});
                    emit(b, MirLabelInstruction{body});
                    auto result = lower_block(b, ws->body);
                    if (result == LowerResult::FallsThrough) {
                        emit(b, MirJump{top});
                    }
                    // Loop itself always falls through — the condition could be false
                    // on entry, so we always reach `end`. Emit it and return FallsThrough.
                    emit(b, MirLabelInstruction{end});
                    return LowerResult::FallsThrough;
                }
                case StmtKind::For: {
                    auto *fs = static_cast<const ast::ForStmt *>(s);
                    if (fs->init) lower_stmt(b, fs->init);
                    auto top = new_label(b);
                    auto body = new_label(b);
                    auto upd = new_label(b);
                    auto end = new_label(b);
                    emit(b, MirLabelInstruction{top});
                    if (fs->condition) {
                        MirValue cond = lower_expr(b, fs->condition);
                        emit(b, MirBranch{cond, body, end});
                    } else {
                        emit(b, MirJump{body});
                    }
                    emit(b, MirLabelInstruction{body});
                    auto result = lower_block(b, fs->body);
                    if (result == LowerResult::FallsThrough) {
                        emit(b, MirLabelInstruction{upd});
                        if (fs->update) static_cast<void>(lower_expr(b, fs->update));
                        emit(b, MirJump{top});
                    }
                    emit(b, MirLabelInstruction{end});
                    return LowerResult::FallsThrough;
                }
                case StmtKind::ForIn:
                    frappe::diagnostics::report_error(b->diag, s->span,
                                                      "for-in not supported in MIR v1 (needs len intrinsic)");
                    return LowerResult::FallsThrough;

                case StmtKind::Switch: {
                    auto *sw = static_cast<const ast::SwitchStmt *>(s);
                    MirValue subject = lower_expr(b, sw->subject);
                    auto end = new_label(b);
                    bool any_falls = false;

                    for (auto *cc: sw->cases) {
                        for (auto *val: cc->values) {
                            MirValue v = lower_expr(b, val);
                            auto match = new_temp(b, b->checker->store.bool_type);
                            emit(b, MirBinary{MirBinaryOp::Eq, match, subject, v});
                            auto body_l = new_label(b);
                            auto next_l = new_label(b);
                            emit(b, MirBranch{MirValue{match}, body_l, next_l});
                            emit(b, MirLabelInstruction{body_l});

                            auto body_result = LowerResult::FallsThrough;
                            for (auto *inner: cc->body) {
                                if (lower_stmt(b, inner) == LowerResult::Terminated) {
                                    body_result = LowerResult::Terminated;
                                    break;
                                }
                            }
                            if (body_result == LowerResult::FallsThrough) {
                                emit(b, MirJump{end});
                                any_falls = true;
                            }
                            emit(b, MirLabelInstruction{next_l});
                        }
                    }

                    const bool has_default = !sw->default_body.empty();
                    auto default_result = LowerResult::Terminated;
                    if (has_default) {
                        default_result = LowerResult::FallsThrough;
                        for (auto *inner: sw->default_body) {
                            if (lower_stmt(b, inner) == LowerResult::Terminated) {
                                default_result = LowerResult::Terminated;
                                break;
                            }
                        }
                        if (default_result == LowerResult::FallsThrough) {
                            emit(b, MirJump{end});
                            any_falls = true;
                        }
                    } else {
                        emit(b, MirJump{end});
                        any_falls = true;
                    }

                    if (!any_falls) {
                        return LowerResult::Terminated;
                    }

                    emit(b, MirLabelInstruction{end});
                    return LowerResult::FallsThrough;
                }
                case StmtKind::Return: {
                    auto *rs = static_cast<const ast::ReturnStmt *>(s);
                    std::optional<MirValue> v;
                    if (rs->value) {
                        MirValue raw = lower_expr(b, rs->value);

                        // Capture value-into-temp only if raw is a mutable local reference.
                        // MirTemp / MirConstantId are immutable in MIR (SSA-like), so
                        // they don't need the extra move. MirLocalId is mutable and can
                        // be clobbered by a deferred write before the return executes.
                        if (std::holds_alternative<MirLocalId>(raw)) {
                            const Type *ty = type_of(b, rs->value);
                            if (!ty) ty = b->checker->store.error_type;
                            auto tmp = new_temp(b, ty);
                            emit(b, MirMove{tmp, raw});
                            v = MirValue{tmp};
                        } else {
                            v = raw;
                        }
                    }
                    emit_all_deferred(b);
                    emit(b, MirReturn{v});
                    return LowerResult::Terminated;
                }
                case StmtKind::Defer: {
                    auto *ds = static_cast<const ast::DeferStmt *>(s);

                    auto saved = std::move(b->current_fn->instructions);
                    b->current_fn->instructions = std::pmr::vector<MirInstruction>{b->mr};

                    if (ds->body) static_cast<void>(lower_stmt(b, ds->body)); // ← stmt, not expr

                    auto queued = std::move(b->current_fn->instructions);
                    b->current_fn->instructions = std::move(saved);

                    b->defer_stack.back().push_back(Defer_Block{std::move(queued)});
                    return LowerResult::FallsThrough;
                }
                case StmtKind::Panic: {
                    auto *ps = static_cast<const ast::PanicStmt *>(s);
                    MirValue msg = lower_expr(b, ps->message);
                    // Deliberately do NOT emit deferred — panic is an immediate abort,
                    // not an unwind. Cleanup does not run.
                    emit(b, MirPanic{msg});
                    return LowerResult::Terminated;
                }
                case StmtKind::Break:
                case StmtKind::Continue:
                    frappe::diagnostics::report_error(b->diag, s->span,
                                                      "break/continue not supported in MIR v1");
                    return LowerResult::Terminated;
            }
            return LowerResult::FallsThrough;
        }

        LowerResult lower_block(MirBuilder *b, const ast::BlockStmt *blk) {
            if (!blk) return LowerResult::FallsThrough;

            b->defer_stack.push_back(std::pmr::vector<Defer_Block>{b->mr});

            LowerResult result = LowerResult::FallsThrough;
            for (auto *s: blk->stmts) {
                if (lower_stmt(b, s) == LowerResult::Terminated) {
                    result = LowerResult::Terminated;
                    break;
                }
            }

            if (result == LowerResult::FallsThrough) {
                emit_deferred(b, b->defer_stack.back());
            }
            b->defer_stack.pop_back();

            return result;
        }

        [[nodiscard]] MirValue lower_ident(MirBuilder *b, const ast::IdentExpr *ie) {
            auto *sym = resolver::lookup(b->checker->resolver_, ie);
            if (!sym) return nil_value(b);
            auto it = b->scope.find(sym);
            if (it == b->scope.end()) {
                FRAPPE_UNREACHABLE(ie->name.span, "symbol '{}' not lowered", ie->name.text);
            }
            return MirValue{it->second};
        }

        [[nodiscard]] MirValue lower_this(MirBuilder *b, source::Span span) {
            if (!b->current_decl) {
                FRAPPE_UNREACHABLE(span, "'this' reached MIR outside a method");
            }
            auto it = b->checker->resolver_->this_syms.find(b->current_decl);
            if (it == b->checker->resolver_->this_syms.end()) {
                FRAPPE_UNREACHABLE(span, "resolver didn't record this_syms for method");
            }
            auto sit = b->scope.find(it->second);
            if (sit == b->scope.end()) {
                FRAPPE_UNREACHABLE(span, "resolver didn't record this_syms for method");
            }
            return MirValue{sit->second};
        }

        [[nodiscard]] MirValue lower_binary(MirBuilder *b, const ast::BinaryExpr *be) {
            MirValue lhs = lower_expr(b, be->left);
            MirValue rhs = lower_expr(b, be->right);
            const Type *ty = type_of(b, be);
            if (!ty) ty = b->checker->store.error_type;

            if (be->op == ast::BinaryOp::And || be->op == ast::BinaryOp::Or) {
                auto dest = new_temp(b, ty);
                auto check_rhs = new_label(b);
                auto end = new_label(b);
                emit(b, MirMove{dest, lhs});
                if (be->op == ast::BinaryOp::And) {
                    emit(b, MirBranch{lhs, check_rhs, end});
                } else {
                    emit(b, MirBranch{lhs, end, check_rhs});
                }
                emit(b, MirLabelInstruction{check_rhs});
                emit(b, MirMove{dest, rhs});
                emit(b, MirLabelInstruction{end});
                return MirValue{dest};
            }

            auto dest = new_temp(b, ty);
            emit(b, MirBinary{map_bin(be->op), dest, lhs, rhs});
            return MirValue{dest};
        }


        [[nodiscard]] MirValue lower_array(MirBuilder *b, const ast::ArrayExpr *ae) {
            std::pmr::vector<MirValue> elems{b->mr};
            for (auto *e: ae->elements) elems.push_back(lower_expr(b, e));
            const Type *ty = type_of(b, ae);
            if (!ty) ty = b->checker->store.error_type;
            auto dest = new_temp(b, ty);
            emit(b, MirBuildArray{dest, std::move(elems)});
            return MirValue{dest};
        }

        [[nodiscard]] MirValue lower_member(MirBuilder *b, const ast::MemberExpr *me) {
            MirValue object = lower_expr(b, me->object);
            const Type *ty = type_of(b, me);
            if (!ty) ty = b->checker->store.error_type;
            auto dest = new_temp(b, ty);
            emit(b, MirGetField{dest, object, me->name.text});
            return MirValue{dest};
        }

        [[nodiscard]] MirValue lower_index(MirBuilder *b, const ast::IndexExpr *ie) {
            MirValue obj = lower_expr(b, ie->object);
            MirValue idx = lower_expr(b, ie->index);
            const Type *ty = type_of(b, ie);
            if (!ty) ty = b->checker->store.error_type;
            auto dest = new_temp(b, ty);
            emit(b, MirGetIndex{dest, obj, idx});
            return MirValue{dest};
        }

        [[nodiscard]] MirValue lower_new(MirBuilder *b, const ast::NewExpr *ne) {
            std::string_view class_name;
            if (ne->type_ && ne->type_->kind == ast::TypeKind::Named) {
                auto *nt = static_cast<const ast::NamedType *>(ne->type_);
                class_name = nt->name.text;
            }

            std::pmr::vector<MirValue> args{b->mr};
            for (auto *arg: ne->args) {
                args.push_back(lower_expr(b, arg));
            }

            const Type *ty = type_of(b, ne);
            if (!ty) ty = b->checker->store.error_type;
            auto dest = new_temp(b, ty);
            emit(b, MirNewObject{dest, class_name, std::move(args)});
            return MirValue{dest};
        }

        [[nodiscard]] MirValue lower_unary(MirBuilder *b, const ast::UnaryExpr *ue) {
            MirValue operand = lower_expr(b, ue->operand);
            const Type *ty = type_of(b, ue);
            if (!ty) ty = b->checker->store.error_type;
            auto dest = new_temp(b, ty);
            emit(b, MirUnary{map_un(ue->op), dest, operand});
            return MirValue{dest};
        }

        void emit_store(MirBuilder *b, const ast::Expr *target, MirValue value) {
            using ast::ExprKind;
            if (target->kind == ExprKind::Ident) {
                auto *ie = static_cast<const ast::IdentExpr *>(target);
                if (auto *sym = resolver::lookup(b->checker->resolver_, ie); sym) {
                    if (auto it = b->scope.find(sym); it != b->scope.end()) {
                        emit(b, MirStoreLocal{it->second, value});
                    }
                }
            } else if (target->kind == ExprKind::Member) {
                auto *me = static_cast<const ast::MemberExpr *>(target);
                MirValue obj = lower_expr(b, me->object);
                emit(b, MirSetField{obj, me->name.text, value});
            } else if (target->kind == ExprKind::Index) {
                auto *ix = static_cast<const ast::IndexExpr *>(target);
                MirValue obj = lower_expr(b, ix->object);
                MirValue idx = lower_expr(b, ix->index);
                emit(b, MirSetIndex{obj, idx, value});
            }
        }

        [[nodiscard]] MirValue lower_call(MirBuilder *b, const ast::CallExpr *call) {
            const Type *ty = type_of(b, call);
            bool is_void = !ty || (ty->kind == TypeKind::Builtin
                                   && static_cast<const types::BuiltinType *>(ty)->b == resolver::Builtin::Void);

            std::pmr::vector<MirValue> args{b->mr};
            std::string_view callee_name = call_name_for(b, call);

            // Fallback: only free function calls can be resolved without call_names.
            // Method calls and generic instances must have been recorded by the checker.
            if (callee_name.empty() && call->callee->kind == ast::ExprKind::Ident) {
                auto *ie = static_cast<const ast::IdentExpr *>(call->callee);
                if (auto *sym = resolver::lookup(b->checker->resolver_, ie); sym) {
                    if (sym->kind == resolver::SymbolKind::Fn && sym->decl.fn) {
                        callee_name = sym->decl.fn->name.text;
                    }
                }
            }

            if (callee_name.empty()) {
                FRAPPE_UNREACHABLE(call->span,
                                   "no callee name for call — checker's record_call_name missed");
            }

            // Receiver-first for method calls
            if (call->callee->kind == ast::ExprKind::Member) {
                auto *me = static_cast<const ast::MemberExpr *>(call->callee);
                bool is_static_call = false;
                if (me->object->kind == ast::ExprKind::Ident) {
                    auto *ie = static_cast<const ast::IdentExpr *>(me->object);
                    if (auto *sym = resolver::lookup(b->checker->resolver_, ie);
                        sym && sym->kind == resolver::SymbolKind::Class) {
                        is_static_call = true;
                    }
                }
                if (!is_static_call) {
                    if (me->object->kind == ast::ExprKind::Super) {
                        args.push_back(lower_this(b, me->object->span));
                    } else {
                        args.push_back(lower_expr(b, me->object));
                    }
                }
            }

            for (auto *arg: call->args) args.push_back(lower_expr(b, arg));

            if (is_void) {
                emit(b, MirCall{std::nullopt, MirDirectCallee{callee_name}, std::move(args)});
                return nil_value(b);
            }

            auto dest = new_temp(b, ty);
            emit(b, MirCall{dest, MirDirectCallee{callee_name}, std::move(args)});
            return MirValue{dest};
        }

        [[nodiscard]] MirValue lower_assign(MirBuilder *b, const ast::AssignExpr *ae) {
            using ast::AssignOp;

            if (ae->op == AssignOp::Assign) {
                MirValue value = lower_expr(b, ae->right);
                emit_store(b, ae->left, value);
                return value;
            }

            MirValue current = lower_expr(b, ae->left);
            MirValue rhs = lower_expr(b, ae->right);
            const Type *ty = type_of(b, ae->left);
            if (!ty) ty = b->checker->store.error_type;
            auto dest = new_temp(b, ty);
            MirBinaryOp op = MirBinaryOp::Add;
            switch (ae->op) {
                case AssignOp::AddAssign: op = MirBinaryOp::Add;
                    break;
                case AssignOp::SubAssign: op = MirBinaryOp::Sub;
                    break;
                case AssignOp::MulAssign: op = MirBinaryOp::Mul;
                    break;
                case AssignOp::DivAssign: op = MirBinaryOp::Div;
                    break;
                case AssignOp::ModAssign: op = MirBinaryOp::Mod;
                    break;
                case AssignOp::Assign: break;
            }
            emit(b, MirBinary{op, dest, current, rhs});
            MirValue result{dest};
            emit_store(b, ae->left, result);
            return result;
        }


        [[nodiscard]] MirValue lower_expr(MirBuilder *b, const ast::Expr *e) {
            using ast::ExprKind;
            if (!e) return nil_value(b);
            switch (e->kind) {
                case ExprKind::Int: {
                    auto *ie = static_cast<const ast::IntExpr *>(e);
                    return MirValue{add_constant(b, MirConstant{ie->value})};
                }
                case ExprKind::Float: {
                    auto *fe = static_cast<const ast::FloatExpr *>(e);
                    return MirValue{add_constant(b, MirConstant{fe->value})};
                }
                case ExprKind::String: {
                    auto *se = static_cast<const ast::StringExpr *>(e);
                    return MirValue{add_constant(b, MirConstant{se->value})};
                }
                case ExprKind::Bool: {
                    auto *be = static_cast<const ast::BoolExpr *>(e);
                    return MirValue{add_constant(b, MirConstant{be->value})};
                }
                case ExprKind::Nil:
                    return nil_value(b);
                case ExprKind::Ident: return lower_ident(b, static_cast<const ast::IdentExpr *>(e));
                case ExprKind::This: return lower_this(b, e->span);
                case ExprKind::Super:
                    FRAPPE_UNREACHABLE(e->span, "bare SuperExpr reached MIR — should be consumed at call site");
                case ExprKind::Binary: return lower_binary(b, static_cast<const ast::BinaryExpr *>(e));
                case ExprKind::Unary: return lower_unary(b, static_cast<const ast::UnaryExpr *>(e));
                case ExprKind::Call: return lower_call(b, static_cast<const ast::CallExpr *>(e));
                case ExprKind::Member: return lower_member(b, static_cast<const ast::MemberExpr *>(e));
                case ExprKind::Index: return lower_index(b, static_cast<const ast::IndexExpr *>(e));
                case ExprKind::Assign: return lower_assign(b, static_cast<const ast::AssignExpr *>(e));
                case ExprKind::New: return lower_new(b, static_cast<const ast::NewExpr *>(e));
                case ExprKind::Array: return lower_array(b, static_cast<const ast::ArrayExpr *>(e));
                case ExprKind::Group:
                    return lower_expr(b, static_cast<const ast::GroupExpr *>(e)->expr);
                case ExprKind::Tuple:
                    frappe::diagnostics::report_error(b->diag, e->span,
                                                      "tuple expressions not supported in MIR v1");
                    return nil_value(b);
            }
            return nil_value(b);
        }


        void begin_function(MirBuilder *b, std::string_view name, const Type *ret_type) {
            b->module_.functions.push_back(MirFunction{
                .symbol = nullptr,
                .name = std::pmr::string{name, b->mr},
                .return_type = ret_type,
                .parameters = std::pmr::vector<MirLocalId>{b->mr},
                .locals = std::pmr::vector<MirLocal>{b->mr},
                .constants = std::pmr::vector<MirConstant>{b->mr},
                .instructions = std::pmr::vector<MirInstruction>{b->mr},
                .temp_types = std::pmr::vector<const Type *>{b->mr},
            });
            b->current_fn = &b->module_.functions.back();
            b->next_temp = 0;
            b->next_label = 0;
            b->scope.clear();
            b->defer_stack.clear();
        }


        void bind_method_receiver(MirBuilder *b, const ast::FnDecl *fn) {
            auto it = b->checker->resolver_->this_syms.find(fn);
            if (it == b->checker->resolver_->this_syms.end()) return;

            const Type *class_ty = b->checker->store.unknown_type;
            if (b->current_class) {
                class_ty = types::named_type(&b->checker->store, types::NamedTypeDeclRef{
                                                 .kind = types::NamedDeclKind::Class,
                                                 .class_ = b->current_class,
                                             });
            }
            auto id = add_local(b, "this", class_ty);
            b->current_fn->parameters.push_back(id);
            b->scope[it->second] = id;
        }

        void lower_fn_decl(MirBuilder *b, const ast::FnDecl *fn, bool is_method) {
            if (!fn->type_params.empty()) return;
            if (!fn->body) return;

            const Type *ret_type = fn->return_type
                                       ? resolve_type_node(b, fn->return_type)
                                       : b->checker->store.void_type;

            std::pmr::string name{b->mr};
            if (is_method && b->current_class) {
                name += b->current_class->name.text;
                name += '.';
            }
            name += fn->name.text;

            b->current_inst = nullptr;
            b->current_decl = fn;
            begin_function(b, name, ret_type);

            if (is_method && !fn->is_static) bind_method_receiver(b, fn);

            for (auto *p: fn->params) {
                const Type *pt = p->type_
                                     ? resolve_type_node(b, p->type_)
                                     : b->checker->store.unknown_type;
                auto id = add_local(b, p->name.text, pt);
                b->current_fn->parameters.push_back(id);
                if (auto *sym = resolver::lookup(b->checker->resolver_, p); sym) {
                    b->scope[sym] = id;
                }
            }

            if (lower_block(b, fn->body) == LowerResult::FallsThrough) {
                emit(b, MirReturn{std::nullopt});
            }

            b->current_fn = nullptr;
            b->current_decl = nullptr;
        }


        void lower_checked_fn(MirBuilder *b, const Checked_Fn *inst) {
            // Generic method — skip receiver for static. (Note: static generic methods
            // don't exist in v1 — checker rejects generic init/deinit, and static
            // generic methods are a v2 feature. So this branch is dead for now.)
            // Static instances of Checked_Fn aren't created. If they ever are,
            // this is where the check goes.
            b->current_inst = inst;
            b->current_decl = inst->decl;
            begin_function(b, inst->name, inst->return_type);
            for (std::size_t i = 0; i < inst->decl->params.size(); ++i) {
                auto *p = inst->decl->params[i];
                auto id = add_local(b, p->name.text, inst->param_types[i]);
                b->current_fn->parameters.push_back(id);
                if (auto *sym = resolver::lookup(b->checker->resolver_, p); sym) {
                    b->scope[sym] = id;
                }
            }
            if (lower_block(b, inst->decl->body) == LowerResult::FallsThrough) {
                emit(b, MirReturn{std::nullopt});
            }

            b->current_inst = nullptr;
            b->current_decl = nullptr;
            b->current_fn = nullptr;
        }

        void build_class(MirBuilder *b, const ast::ClassDecl *cd) {
            MirClass mc{
                .name = cd->name.text,
                .base_name = std::nullopt,
                .fields = std::pmr::vector<MirField>{b->mr},
                .methods = std::pmr::vector<MirMethod>{b->mr},
                .has_init = false,
                .has_deinit = false,
                .has_vtable = false,
                .is_final = cd->is_final,
                .is_abstract = cd->is_abstract,
            };

            if (cd->base && cd->base->kind == ast::TypeKind::Named) {
                auto *bnt = static_cast<const ast::NamedType *>(cd->base);
                mc.base_name = bnt->name.text;
            }

            // Inherited fields come first — codegen uses field order to build
            // the LLVM struct type; LLVM computes byte offsets.
            if (mc.base_name) {
                for (const auto &base_mc: b->module_.classes) {
                    if (base_mc.name == *mc.base_name) {
                        for (const auto &bf: base_mc.fields) {
                            mc.fields.push_back(MirField{
                                .name = bf.name,
                                .type = bf.type,
                                .index = static_cast<std::uint32_t>(mc.fields.size()),
                            });
                        }
                        break;
                    }
                }
            }

            // Own fields
            for (auto *field: cd->fields) {
                const Type *ftype = field->type_
                                        ? resolve_type_node(b, field->type_)
                                        : b->checker->store.unknown_type;
                mc.fields.push_back(MirField{
                    .name = field->name.text,
                    .type = ftype,
                    .index = static_cast<std::uint32_t>(mc.fields.size()),
                });
            }

            // Own methods
            for (auto *m: cd->methods) {
                if (m->name.text == frappe::syntax::INIT_NAME) mc.has_init = true;
                if (m->name.text == frappe::syntax::DEINIT_NAME) mc.has_deinit = true;

                std::pmr::string full{b->mr};
                full += cd->name.text;
                full += '.';
                full += m->name.text;
                mc.methods.push_back(MirMethod{
                    .name = m->name.text,
                    .fn_name = intern_string(b, full),
                });
            }

            // Inherit base class methods not overridden locally.
            // Their fn_name string_views point into the base class's arena storage —
            // safe to copy directly.
            if (mc.base_name) {
                for (const auto &base_mc: b->module_.classes) {
                    if (base_mc.name == *mc.base_name) {
                        for (const auto &bm: base_mc.methods) {
                            bool already = false;
                            for (const auto &m: mc.methods) {
                                if (m.name == bm.name) {
                                    already = true;
                                    break;
                                }
                            }
                            if (!already) mc.methods.push_back(bm);
                        }
                        break;
                    }
                }
            }

            b->module_.classes.push_back(std::move(mc));

            // Lower method bodies
            auto *old_class = b->current_class;
            b->current_class = cd;
            for (auto *m: cd->methods) {
                if (m->body && m->type_params.empty()) {
                    lower_fn_decl(b, m, true);
                }
            }
            b->current_class = old_class;
        }
    }

    MirModule build_module(const ast::Program &program,
                           frappe::checker::Checker *checker,
                           frappe::diagnostics::Engine *diag,
                           std::pmr::memory_resource *mr) {
        MirBuilder b{
            .mr = mr,
            .checker = checker,
            .diag = diag,
            .module_ = MirModule{
                .functions = std::pmr::vector<MirFunction>{mr},
                .classes = std::pmr::vector<MirClass>{mr},
            },
            .scope = std::pmr::unordered_map<const resolver::Symbol *, MirLocalId>{mr},
            .defer_stack = std::pmr::vector<std::pmr::vector<Defer_Block> >{mr},

        };

        // -----------------------------------------------------------------
        // Pass 1: classes — tri-color DFS to order base-before-derived
        // and detect inheritance cycles.
        // -----------------------------------------------------------------
        std::pmr::unordered_map<std::string_view, const ast::ClassDecl *> by_name{b.mr};
        for (auto *d: program.decls) {
            if (d->kind == ast::DeclKind::Class) {
                auto *cd = static_cast<const ast::ClassDecl *>(d);
                by_name[cd->name.text] = cd;
            }
        }

        std::pmr::unordered_map<const ast::ClassDecl *, Color> colors{b.mr};
        std::pmr::vector<const ast::ClassDecl *> ordered{b.mr};

        std::function<bool(const ast::ClassDecl *)> visit =
                [&](const ast::ClassDecl *cd) -> bool {
            if (frappe::diagnostics::has_errors(b.diag)) return false;

            auto &color = colors[cd];
            if (color == Color::Black) return true; // done — legitimate revisit

            if (color == Color::Gray) {
                FRAPPE_UNREACHABLE(cd->name.span,
                                   "inheritance cycle reached MIR — resolver's validate_inheritance missed it");
            }

            color = Color::Gray;
            if (cd->base && cd->base->kind == ast::TypeKind::Named) {
                auto *bnt = static_cast<const ast::NamedType *>(cd->base);
                if (auto it = by_name.find(bnt->name.text); it != by_name.end()) {
                    if (!visit(it->second)) return false;
                }
            }
            color = Color::Black;
            ordered.push_back(cd);
            return true;
        };

        for (auto *d: program.decls) {
            if (d->kind == ast::DeclKind::Class) {
                if (!visit(static_cast<const ast::ClassDecl *>(d))) break;
            }
        }

        // If cycle detection aborted, ordered is partial — skip build_class
        // and let main.cpp surface the diagnostic.
        if (!frappe::diagnostics::has_errors(b.diag)) {
            for (auto *cd: ordered) {
                build_class(&b, cd);
            }
        }

        // -----------------------------------------------------------------
        // Pass 2: non-generic free functions
        // -----------------------------------------------------------------
        for (auto *d: program.decls) {
            if (d->kind == ast::DeclKind::Fn) {
                auto *fn = static_cast<const ast::FnDecl *>(d);
                if (fn->type_params.empty()) {
                    lower_fn_decl(&b, fn, false);
                }
            }
        }

        // -----------------------------------------------------------------
        // Pass 3: generic instances (all monomorphic, discovered by checker)
        // -----------------------------------------------------------------
        for (const auto &[name, inst]: checker->instances) {
            (void) name;
            lower_checked_fn(&b, inst);
        }

        return std::move(b.module_);
    }
}
