//
// Created by antar on 19/10/26.
//
#include <frappe/checker/checker.hpp>

#include <span>
#include <string_view>

#include <frappe/ast/expr.hpp>

#include "checker_internal.hpp"

namespace frappe::checker::detail {
    namespace {
        using ast::ExprKind;
        using types::Type;
        using types::TypeKind;

        Type *bind_expr(Checker *c, const void *node, Type *t) {
            set_type(c, node, t);
            return t;
        }

        // --- Literals ---------------------------------------------------------------

        Type *check_int_lit(Checker *c, const ast::IntExpr *e) { return bind_expr(c, e, c->store.int_type); }
        Type *check_float_lit(Checker *c, const ast::FloatExpr *e) { return bind_expr(c, e, c->store.float_type); }
        Type *check_string_lit(Checker *c, const ast::StringExpr *e) { return bind_expr(c, e, c->store.string_type); }
        Type *check_bool_lit(Checker *c, const ast::BoolExpr *e) { return bind_expr(c, e, c->store.bool_type); }
        Type *check_nil_lit(Checker *c, const ast::NilExpr *e) { return bind_expr(c, e, c->store.nil_type); }

        // --- Identifiers ------------------------------------------------------------

        Type *check_ident(Checker *c, const ast::IdentExpr *ie) {
            auto *sym = resolver::lookup(c->resolver_, ie);
            if (!sym) return bind_expr(c, ie, c->store.error_type);

            switch (sym->kind) {
                case resolver::SymbolKind::Class:
                case resolver::SymbolKind::Interface:
                case resolver::SymbolKind::Enum:
                case resolver::SymbolKind::BuiltinType:
                    diagnostics::report_error(c->diag, ie->name.span,
                                              "type '{}' used as value", ie->name.text);
                    return bind_expr(c, ie, c->store.error_type);
                case resolver::SymbolKind::Module:
                case resolver::SymbolKind::Var:
                case resolver::SymbolKind::LocalVar:
                case resolver::SymbolKind::ForInVar:
                case resolver::SymbolKind::Param:
                case resolver::SymbolKind::Fn:
                case resolver::SymbolKind::Method:
                case resolver::SymbolKind::Field:
                case resolver::SymbolKind::TypeParam:
                case resolver::SymbolKind::This:
                case resolver::SymbolKind::Super:
                    return bind_expr(c, ie, type_of_symbol(c, sym));
            }
            return bind_expr(c, ie, c->store.error_type);
        }

        Type *check_group(Checker *c, const ast::GroupExpr *ge) {
            return bind_expr(c, ge, check_expr(c, ge->expr));
        }

        // --- Numeric helpers --------------------------------------------------------

        bool is_numeric(const Type *t) {
            if (!t || t->kind != TypeKind::Builtin) return false;
            auto *bt = static_cast<const types::BuiltinType *>(t);
            switch (bt->b) {
                case resolver::Builtin::Int:
                case resolver::Builtin::Float:
                case resolver::Builtin::Int8:
                case resolver::Builtin::Int16:
                case resolver::Builtin::Int32:
                case resolver::Builtin::Int64:
                case resolver::Builtin::UInt8:
                case resolver::Builtin::UInt16:
                case resolver::Builtin::UInt32:
                case resolver::Builtin::UInt64:
                case resolver::Builtin::Float32:
                case resolver::Builtin::Float64:
                    return true;
                case resolver::Builtin::Bool:
                case resolver::Builtin::String:
                case resolver::Builtin::Char:
                case resolver::Builtin::Nil:
                case resolver::Builtin::Void:
                    return false;
            }
            return false;
        }

        bool is_float(const Type *t) {
            if (!t || t->kind != TypeKind::Builtin) return false;
            auto *bt = static_cast<const types::BuiltinType *>(t);
            return bt->b == resolver::Builtin::Float
                   || bt->b == resolver::Builtin::Float32
                   || bt->b == resolver::Builtin::Float64;
        }

        Type *numeric_result(Checker *c, const Type *lt, const Type *rt,
                             const source::Span &span, std::string_view what) {
            if (!is_numeric(lt) || !is_numeric(rt)) {
                diagnostics::report_error(c->diag, span,
                                          "{} requires numeric operands", what);
                return c->store.error_type;
            }
            return (is_float(lt) || is_float(rt)) ? c->store.float_type : c->store.int_type;
        }

        // --- Binary / Unary ---------------------------------------------------------

        Type *check_binary(Checker *c, const ast::BinaryExpr *be) {
            auto *lt = check_expr(c, be->left);
            auto *rt = check_expr(c, be->right);
            if (types::is_error(lt) || types::is_error(rt)) {
                return bind_expr(c, be, c->store.error_type);
            }

            Type *result = c->store.error_type;
            switch (be->op) {
                case ast::BinaryOp::Add:
                    if (types::type_eq(lt, c->store.string_type)
                        && types::type_eq(rt, c->store.string_type)) {
                        result = c->store.string_type;
                    } else {
                        result = numeric_result(c, lt, rt, be->op_span, "addition");
                    }
                    break;
                case ast::BinaryOp::Sub:
                case ast::BinaryOp::Mul:
                case ast::BinaryOp::Div:
                case ast::BinaryOp::Mod:
                    result = numeric_result(c, lt, rt, be->op_span, "arithmetic");
                    break;
                case ast::BinaryOp::Lt:
                case ast::BinaryOp::Lte:
                case ast::BinaryOp::Gt:
                case ast::BinaryOp::Gte:
                    if (!is_numeric(lt) || !is_numeric(rt)) {
                        diagnostics::report_error(c->diag, be->op_span,
                                                  "comparison requires numeric operands");
                    } else {
                        result = c->store.bool_type;
                    }
                    break;
                case ast::BinaryOp::Eq:
                case ast::BinaryOp::Neq:
                    if (!types::type_eq(lt, rt)) {
                        diagnostics::report_error(c->diag, be->op_span,
                                                  "cannot compare {} and {}",
                                                  types::type_name(c->store.mr, lt),
                                                  types::type_name(c->store.mr, rt));
                    } else {
                        result = c->store.bool_type;
                    }
                    break;
                case ast::BinaryOp::And:
                case ast::BinaryOp::Or:
                    if (!types::type_eq(lt, c->store.bool_type)
                        || !types::type_eq(rt, c->store.bool_type)) {
                        diagnostics::report_error(c->diag, be->op_span,
                                                  "logical operator requires Bool operands");
                    } else {
                        result = c->store.bool_type;
                    }
                    break;
            }
            return bind_expr(c, be, result);
        }

        Type *check_unary(Checker *c, const ast::UnaryExpr *ue) {
            auto *ot = check_expr(c, ue->operand);
            if (types::is_error(ot)) return bind_expr(c, ue, c->store.error_type);

            Type *result = c->store.error_type;
            switch (ue->op) {
                case ast::UnaryOp::Neg:
                    if (!is_numeric(ot)) {
                        diagnostics::report_error(c->diag, ue->op_span,
                                                  "unary '-' requires numeric operand");
                    } else {
                        result = ot;
                    }
                    break;
                case ast::UnaryOp::Not:
                    if (!types::type_eq(ot, c->store.bool_type)) {
                        diagnostics::report_error(c->diag, ue->op_span,
                                                  "unary '!' requires Bool operand");
                    } else {
                        result = c->store.bool_type;
                    }
                    break;
            }
            return bind_expr(c, ue, result);
        }

        // --- Assignment -------------------------------------------------------------

        bool is_lvalue(const ast::Expr *e) {
            return e->kind == ExprKind::Ident
                   || e->kind == ExprKind::Member
                   || e->kind == ExprKind::Index;
        }

        Type *check_assign(Checker *c, const ast::AssignExpr *ae) {
            auto *lt = check_expr(c, ae->left);
            auto *rt = check_expr(c, ae->right);
            if (types::is_error(lt) || types::is_error(rt)) {
                return bind_expr(c, ae, c->store.error_type);
            }
            if (!is_lvalue(ae->left)) {
                diagnostics::report_error(c->diag, ae->left->span,
                                          "left side of assignment is not assignable");
                return bind_expr(c, ae, c->store.error_type);
            }
            // final field check fires for both x = y and x += y
            if (ae->left->kind == ast::ExprKind::Member) {
                auto *me = static_cast<const ast::MemberExpr *>(ae->left);
                check_field_assign(c, me);
            }

            if (ae->op != ast::AssignOp::Assign) {
                if (!is_numeric(lt) || !is_numeric(rt)) {
                    diagnostics::report_error(c->diag, ae->op_span,
                                              "compound assignment requires numeric operands");
                    return bind_expr(c, ae, c->store.error_type);
                }
                return bind_expr(c, ae, lt);
            }

            check_assignable(c, lt, rt, ae->right->span);
            return bind_expr(c, ae, lt);
        }

        // --- Calls ------------------------------------------------------------------

        Type *check_call_to_fn(Checker *c, const ast::CallExpr *call, const ast::FnDecl *fn) {
            std::pmr::vector<Type *> expected{c->store.mr};
            expected.reserve(fn->params.size());
            for (auto *p: fn->params) expected.push_back(resolve_ast_type(c, p->type_));

            if (call->args.size() != fn->params.size()) {
                diagnostics::report_error(c->diag, call->span,
                                          "'{}' expects {} arguments, got {}",
                                          fn->name.text, fn->params.size(), call->args.size());
                for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
                return bind_expr(c, call, resolve_ast_type(c, fn->return_type));
            }

            for (std::size_t i = 0; i < call->args.size(); ++i) {
                auto *at = check_expr(c, call->args[i]);
                check_assignable(c, expected[i], at, call->args[i]->span);
            }
            return bind_expr(c, call, resolve_ast_type(c, fn->return_type));
        }

        void check_call_args(Checker *c, const ast::CallExpr *call,
                             std::span<types::Type * const> expected) {
            if (call->args.size() != expected.size()) {
                diagnostics::report_error(c->diag, call->span,
                                          "expected {} arguments, got {}",
                                          expected.size(), call->args.size());
                for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
                return;
            }
            for (std::size_t i = 0; i < call->args.size(); ++i) {
                auto *at = check_expr(c, call->args[i]);
                check_assignable(c, expected[i], at, call->args[i]->span);
            }
        }

        Type *check_call(Checker *c, const ast::CallExpr *call) {
            if (call->callee->kind == ExprKind::Member) {
                return check_method_call(c, call,
                                         static_cast<const ast::MemberExpr *>(call->callee));
            }
            if (call->callee->kind == ExprKind::Ident) {
                auto *ident = static_cast<const ast::IdentExpr *>(call->callee);
                if (auto *sym = resolver::lookup(c->resolver_, ident); sym) {
                    if (sym->kind == resolver::SymbolKind::Fn
                        || sym->kind == resolver::SymbolKind::Method) {
                        auto *fn = sym->decl.fn;
                        if (!fn->type_params.empty()) {
                            return check_generic_call(c, call, fn);
                        }
                        return check_call_to_fn(c, call, fn);
                    }
                }
            }

            auto *ct = check_expr(c, call->callee);
            if (types::is_error(ct)) return bind_expr(c, call, c->store.error_type);

            if (ct->kind != TypeKind::Func) {
                diagnostics::report_error(c->diag, call->span, "'{}' is not callable",
                                          types::type_name(c->store.mr, ct));
                return bind_expr(c, call, c->store.error_type);
            }
            auto *ft = static_cast<const types::FuncType *>(ct);
            check_call_args(c, call, ft->params);
            return bind_expr(c, call, ft->ret);
        }

        // --- Arrays -----------------------------------------------------------------

        Type *check_array(Checker *c, const ast::ArrayExpr *ae) {
            if (ae->elements.empty()) {
                diagnostics::report_error(c->diag, ae->span,
                                          "cannot infer element type of empty array literal");
                return bind_expr(c, ae, c->store.error_type);
            }
            auto *elem = check_expr(c, ae->elements[0]);
            for (std::size_t i = 1; i < ae->elements.size(); ++i) {
                auto *et = check_expr(c, ae->elements[i]);
                check_assignable(c, elem, et, ae->elements[i]->span);
            }
            return bind_expr(c, ae, types::array_type(&c->store, elem, -1));
        }

        Type *check_index(Checker *c, const ast::IndexExpr *ie) {
            auto *obj_ty = check_expr(c, ie->object);
            auto *idx_ty = check_expr(c, ie->index);
            if (types::is_error(obj_ty) || types::is_error(idx_ty)) {
                return bind_expr(c, ie, c->store.error_type);
            }

            if (!types::type_eq(idx_ty, c->store.int_type)) {
                diagnostics::report_error(c->diag, ie->index->span,
                                          "array index must be Int, got {}",
                                          types::type_name(c->store.mr, idx_ty));
                return bind_expr(c, ie, c->store.error_type);
            }

            if (obj_ty->kind != TypeKind::Array) {
                diagnostics::report_error(c->diag, ie->object->span,
                                          "cannot index non-array type {}",
                                          types::type_name(c->store.mr, obj_ty));
                return bind_expr(c, ie, c->store.error_type);
            }

            auto *at = static_cast<const types::ArrayType *>(obj_ty);
            return bind_expr(c, ie, at->element);
        }
    }

    // ============================================================================
    // Public — within detail namespace
    // ============================================================================

    Type *check_expr(Checker *c, const ast::Expr *e) {
        if (!e) return c->store.error_type;
        switch (e->kind) {
            case ExprKind::Int: return check_int_lit(c, static_cast<const ast::IntExpr *>(e));
            case ExprKind::Float: return check_float_lit(c, static_cast<const ast::FloatExpr *>(e));
            case ExprKind::String: return check_string_lit(c, static_cast<const ast::StringExpr *>(e));
            case ExprKind::Bool: return check_bool_lit(c, static_cast<const ast::BoolExpr *>(e));
            case ExprKind::Nil: return check_nil_lit(c, static_cast<const ast::NilExpr *>(e));
            case ExprKind::Ident: return check_ident(c, static_cast<const ast::IdentExpr *>(e));
            case ExprKind::Group: return check_group(c, static_cast<const ast::GroupExpr *>(e));
            case ExprKind::Binary: return check_binary(c, static_cast<const ast::BinaryExpr *>(e));
            case ExprKind::Unary: return check_unary(c, static_cast<const ast::UnaryExpr *>(e));
            case ExprKind::Assign: return check_assign(c, static_cast<const ast::AssignExpr *>(e));
            case ExprKind::Call: return check_call(c, static_cast<const ast::CallExpr *>(e));
            case ExprKind::Array: return check_array(c, static_cast<const ast::ArrayExpr *>(e));
            case ExprKind::This: return check_this(c, static_cast<const ast::ThisExpr *>(e));
            case ExprKind::Super: return check_super(c, static_cast<const ast::SuperExpr *>(e));
            case ExprKind::Member: return check_member(c, static_cast<const ast::MemberExpr *>(e));
            case ExprKind::New: return check_new(c, static_cast<const ast::NewExpr *>(e));

            case ExprKind::Index:
                return check_index(c, static_cast<const ast::IndexExpr *>(e));

            case ExprKind::Tuple:
                diagnostics::report_error(c->diag, e->span,
                                          "expression form not supported in v1");
                return c->store.error_type;
        }
        return c->store.error_type;
    }

    Type *check_expr_against(Checker *c, const ast::Expr *e, Type *expected) {
        if (e && expected
            && e->kind == ExprKind::Array
            && expected->kind == TypeKind::Array) {
            auto *arr = static_cast<const ast::ArrayExpr *>(e);
            if (arr->elements.empty()) {
                set_type(c, arr, expected);
                return expected;
            }
        }
        return check_expr(c, e);
    }
} // namespace frappe::checker::detail
