//
// Created by antar on 19/10/26.
//


#include <frappe/ast_printer/printer.hpp>

#include <cstddef>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

#include <frappe/ast/operator.hpp>

namespace frappe::ast_printer {
    namespace {
        using namespace frappe::ast;

        // ---------------------------------------------------------------------------
        // Primitives
        // ---------------------------------------------------------------------------

        void indent(Printer *p) {
            for (int i = 0; i < p->depth; ++i) p->buf += "  ";
        }

        template<typename... Args>
        void line(Printer *p, std::format_string<Args...> fmt, Args &&... args) {
            indent(p);
            std::format_to(std::back_inserter(p->buf), fmt, std::forward<Args>(args)...);
            p->buf += '\n';
        }

        void write_escaped(Printer *p, std::string_view s) {
            for (char ch: s) {
                auto c = static_cast<unsigned char>(ch);
                switch (ch) {
                    case '\n': p->buf += "\\n";
                        break;
                    case '\t': p->buf += "\\t";
                        break;
                    case '\r': p->buf += "\\r";
                        break;
                    case '"': p->buf += "\\\"";
                        break;
                    case '\\': p->buf += "\\\\";
                        break;
                    default:
                        if (c >= 32 && c < 127) p->buf += ch;
                        else std::format_to(std::back_inserter(p->buf), "<U+{:04X}>", c);
                        break;
                }
            }
        }

        void write_modifiers(Printer *p, Visibility vis,
                             bool is_static, bool is_final,
                             bool is_abstract, bool is_override) {
            std::string_view parts[5];
            int n = 0;
            switch (vis) {
                case Visibility::Private: parts[n++] = "private";
                    break;
                case Visibility::Public: parts[n++] = "public";
                    break;
                case Visibility::Protected: parts[n++] = "protected";
                    break;
                case Visibility::Default: break;
            }
            if (is_static) parts[n++] = "static";
            if (is_final) parts[n++] = "final";
            if (is_abstract) parts[n++] = "abstract";
            if (is_override) parts[n++] = "override";
            if (n == 0) return;

            p->buf += " [";
            for (int i = 0; i < n; ++i) {
                if (i > 0) p->buf += ' ';
                p->buf += parts[i];
            }
            p->buf += ']';
        }

        // ---------------------------------------------------------------------------
        // Forward declarations
        // ---------------------------------------------------------------------------

        void write_type(Printer *p, const Type *t);

        void write_inline_expr(Printer *p, const Expr *e);

        void print_sub_expr(Printer *p, std::string_view name, const Expr *e);

        void print_program(Printer *p, const Program &prog);

        void print_decl(Printer *p, const Decl *d);

        void print_stmt(Printer *p, const Stmt *s);

        void print_expr(Printer *p, const Expr *e);

        void print_block(Printer *p, const BlockStmt *b);

        void print_case_clause(Printer *p, const CaseClause *c);

        void print_fn_decl(Printer *p, const FnDecl *fn);

        void print_field(Printer *p, const FieldDecl *f);

        void print_type_param(Printer *p, const TypeParam *tp);

        void print_param(Printer *p, const Param *param);

        void print_variant(Printer *p, const EnumVariant *v);

        // ---------------------------------------------------------------------------
        // write_type — single-line
        // ---------------------------------------------------------------------------

        void write_type(Printer *p, const Type *t) {
            if (!t) {
                p->buf += "<void>";
                return;
            }
            switch (t->kind) {
                case TypeKind::Named: {
                    auto *nt = static_cast<const NamedType *>(t);
                    p->buf += nt->name.text;
                    break;
                }
                case TypeKind::Array: {
                    auto *at = static_cast<const ArrayType *>(t);
                    p->buf += '[';
                    write_type(p, at->element);
                    if (at->size < 0) p->buf += "; dynamic]";
                    else std::format_to(std::back_inserter(p->buf), "; {}]", at->size);
                    break;
                }
                case TypeKind::Optional: {
                    auto *ot = static_cast<const OptionalType *>(t);
                    write_type(p, ot->inner);
                    p->buf += '?';
                    break;
                }
                case TypeKind::Applied: {
                    auto *ap = static_cast<const AppliedType *>(t);
                    write_type(p, ap->base);
                    p->buf += '<';
                    for (std::size_t i = 0; i < ap->args.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_type(p, ap->args[i]);
                    }
                    p->buf += '>';
                    break;
                }
                case TypeKind::Func: {
                    auto *ft = static_cast<const FuncType *>(t);
                    p->buf += '(';
                    for (std::size_t i = 0; i < ft->params.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_type(p, ft->params[i]);
                    }
                    p->buf += ") -> ";
                    write_type(p, ft->ret);
                    break;
                }
                case TypeKind::Tuple: {
                    auto *tt = static_cast<const TupleType *>(t);
                    p->buf += '(';
                    for (std::size_t i = 0; i < tt->elems.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_type(p, tt->elems[i]);
                    }
                    p->buf += ')';
                    break;
                }
            }
        }

        // ---------------------------------------------------------------------------
        // write_inline_expr — single-line, used in headers
        // ---------------------------------------------------------------------------

        void write_inline_expr(Printer *p, const Expr *e) {
            if (!e) {
                p->buf += "<nil>";
                return;
            }
            switch (e->kind) {
                case ExprKind::Int: {
                    auto *ie = static_cast<const IntExpr *>(e);
                    std::format_to(std::back_inserter(p->buf), "{}", ie->value);
                    break;
                }
                case ExprKind::Float: {
                    auto *fe = static_cast<const FloatExpr *>(e);
                    std::format_to(std::back_inserter(p->buf), "{}", fe->value);
                    break;
                }
                case ExprKind::String: {
                    auto *se = static_cast<const StringExpr *>(e);
                    p->buf += '"';
                    write_escaped(p, se->value);
                    p->buf += '"';
                    break;
                }
                case ExprKind::Bool: {
                    auto *be = static_cast<const BoolExpr *>(e);
                    p->buf += be->value ? "true" : "false";
                    break;
                }
                case ExprKind::Nil: p->buf += "nil";
                    break;
                case ExprKind::Ident: {
                    auto *ie = static_cast<const IdentExpr *>(e);
                    p->buf += ie->name.text;
                    break;
                }
                case ExprKind::This: p->buf += "this";
                    break;
                case ExprKind::Super: p->buf += "super";
                    break;
                case ExprKind::Binary: {
                    auto *be = static_cast<const BinaryExpr *>(e);
                    write_inline_expr(p, be->left);
                    p->buf += ' ';
                    p->buf += binary_op_name(be->op);
                    p->buf += ' ';
                    write_inline_expr(p, be->right);
                    break;
                }
                case ExprKind::Unary: {
                    auto *ue = static_cast<const UnaryExpr *>(e);
                    p->buf += unary_op_name(ue->op);
                    write_inline_expr(p, ue->operand);
                    break;
                }
                case ExprKind::Assign: {
                    auto *ae = static_cast<const AssignExpr *>(e);
                    write_inline_expr(p, ae->left);
                    p->buf += ' ';
                    p->buf += assign_op_name(ae->op);
                    p->buf += ' ';
                    write_inline_expr(p, ae->right);
                    break;
                }
                case ExprKind::Call: {
                    auto *ce = static_cast<const CallExpr *>(e);
                    write_inline_expr(p, ce->callee);
                    p->buf += '(';
                    for (std::size_t i = 0; i < ce->args.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_inline_expr(p, ce->args[i]);
                    }
                    p->buf += ')';
                    break;
                }
                case ExprKind::Member: {
                    auto *me = static_cast<const MemberExpr *>(e);
                    write_inline_expr(p, me->object);
                    p->buf += '.';
                    p->buf += me->name.text;
                    break;
                }
                case ExprKind::Index: {
                    auto *ie = static_cast<const IndexExpr *>(e);
                    write_inline_expr(p, ie->object);
                    p->buf += '[';
                    write_inline_expr(p, ie->index);
                    p->buf += ']';
                    break;
                }
                case ExprKind::New: {
                    auto *ne = static_cast<const NewExpr *>(e);
                    p->buf += "new ";
                    write_type(p, ne->type_);
                    p->buf += '(';
                    for (std::size_t i = 0; i < ne->args.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_inline_expr(p, ne->args[i]);
                    }
                    p->buf += ')';
                    break;
                }
                case ExprKind::Group: {
                    auto *ge = static_cast<const GroupExpr *>(e);
                    p->buf += '(';
                    write_inline_expr(p, ge->expr);
                    p->buf += ')';
                    break;
                }
                case ExprKind::Array: {
                    auto *ae = static_cast<const ArrayExpr *>(e);
                    p->buf += '[';
                    for (std::size_t i = 0; i < ae->elements.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_inline_expr(p, ae->elements[i]);
                    }
                    p->buf += ']';
                    break;
                }
                case ExprKind::Tuple: {
                    auto *te = static_cast<const TupleExpr *>(e);
                    p->buf += '(';
                    for (std::size_t i = 0; i < te->elements.size(); ++i) {
                        if (i > 0) p->buf += ", ";
                        write_inline_expr(p, te->elements[i]);
                    }
                    p->buf += ')';
                    break;
                }
            }
        }

        // ---------------------------------------------------------------------------
        // Leaf printers
        // ---------------------------------------------------------------------------

        void print_type_param(Printer *p, const TypeParam *tp) {
            if (!tp) return;
            line(p, "Type_Param {}", tp->name.text);
            if (tp->bound) {
                p->depth++;
                indent(p);
                p->buf += "bound: ";
                write_type(p, tp->bound);
                p->buf += '\n';
                p->depth--;
            }
        }

        void print_param(Printer *p, const Param *param) {
            if (!param) return;
            indent(p);
            std::format_to(std::back_inserter(p->buf), "Param {}: ", param->name.text);
            write_type(p, param->type_);
            p->buf += '\n';
        }

        void print_variant(Printer *p, const EnumVariant *v) {
            if (!v) return;
            indent(p);
            std::format_to(std::back_inserter(p->buf), "Enum_Variant {}", v->name.text);
            if (v->value) {
                p->buf += " = ";
                write_inline_expr(p, v->value);
            }
            p->buf += '\n';
        }

        // ---------------------------------------------------------------------------
        // Expressions — tree form
        // ---------------------------------------------------------------------------

        void print_sub_expr(Printer *p, std::string_view name, const Expr *e) {
            indent(p);
            p->buf += name;
            p->buf += ":\n";
            p->depth++;
            print_expr(p, e);
            p->depth--;
        }

        void print_expr(Printer *p, const Expr *e) {
            if (!e) {
                line(p, "<nil>");
                return;
            }
            switch (e->kind) {
                case ExprKind::Int: {
                    auto *ie = static_cast<const IntExpr *>(e);
                    line(p, "Int_Lit {}", ie->value);
                    break;
                }
                case ExprKind::Float: {
                    auto *fe = static_cast<const FloatExpr *>(e);
                    line(p, "Float_Lit {}", fe->value);
                    break;
                }
                case ExprKind::String: {
                    auto *se = static_cast<const StringExpr *>(e);
                    indent(p);
                    p->buf += "String_Lit \"";
                    write_escaped(p, se->value);
                    p->buf += "\"\n";
                    break;
                }
                case ExprKind::Bool: {
                    auto *be = static_cast<const BoolExpr *>(e);
                    line(p, "Bool_Lit {}", be->value);
                    break;
                }
                case ExprKind::Nil: line(p, "Nil_Lit");
                    break;
                case ExprKind::Ident: {
                    auto *ie = static_cast<const IdentExpr *>(e);
                    line(p, "Ident_Expr {}", ie->name.text);
                    break;
                }
                case ExprKind::This: line(p, "This_Expr");
                    break;
                case ExprKind::Super: line(p, "Super_Expr");
                    break;
                case ExprKind::Binary: {
                    auto *be = static_cast<const BinaryExpr *>(e);
                    line(p, "Binary_Expr {}", binary_op_name(be->op));
                    p->depth++;
                    print_sub_expr(p, "left", be->left);
                    print_sub_expr(p, "right", be->right);
                    p->depth--;
                    break;
                }
                case ExprKind::Unary: {
                    auto *ue = static_cast<const UnaryExpr *>(e);
                    line(p, "Unary_Expr {}", unary_op_name(ue->op));
                    p->depth++;
                    print_sub_expr(p, "operand", ue->operand);
                    p->depth--;
                    break;
                }
                case ExprKind::Assign: {
                    auto *ae = static_cast<const AssignExpr *>(e);
                    line(p, "Assign_Expr {}", assign_op_name(ae->op));
                    p->depth++;
                    print_sub_expr(p, "left", ae->left);
                    print_sub_expr(p, "right", ae->right);
                    p->depth--;
                    break;
                }
                case ExprKind::Call: {
                    auto *ce = static_cast<const CallExpr *>(e);
                    line(p, "Call_Expr");
                    p->depth++;
                    print_sub_expr(p, "callee", ce->callee);
                    if (!ce->args.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf), "args ({}):\n", ce->args.size());
                        p->depth++;
                        for (auto *arg: ce->args) print_expr(p, arg);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case ExprKind::Member: {
                    auto *me = static_cast<const MemberExpr *>(e);
                    line(p, "Member_Expr .{}", me->name.text);
                    p->depth++;
                    print_sub_expr(p, "object", me->object);
                    p->depth--;
                    break;
                }
                case ExprKind::Index: {
                    auto *ie = static_cast<const IndexExpr *>(e);
                    line(p, "Index_Expr");
                    p->depth++;
                    print_sub_expr(p, "object", ie->object);
                    print_sub_expr(p, "index", ie->index);
                    p->depth--;
                    break;
                }
                case ExprKind::New: {
                    auto *ne = static_cast<const NewExpr *>(e);
                    line(p, "New_Expr");
                    p->depth++;
                    indent(p);
                    p->buf += "type: ";
                    write_type(p, ne->type_);
                    p->buf += '\n';
                    if (!ne->args.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf), "args ({}):\n", ne->args.size());
                        p->depth++;
                        for (auto *arg: ne->args) print_expr(p, arg);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case ExprKind::Group: {
                    auto *ge = static_cast<const GroupExpr *>(e);
                    line(p, "Group_Expr");
                    p->depth++;
                    print_sub_expr(p, "expr", ge->expr);
                    p->depth--;
                    break;
                }
                case ExprKind::Array: {
                    auto *ae = static_cast<const ArrayExpr *>(e);
                    line(p, "Array_Expr ({} elements)", ae->elements.size());
                    p->depth++;
                    for (auto *elem: ae->elements) print_expr(p, elem);
                    p->depth--;
                    break;
                }
                case ExprKind::Tuple: {
                    auto *te = static_cast<const TupleExpr *>(e);
                    line(p, "Tuple_Expr ({} elements)", te->elements.size());
                    p->depth++;
                    for (auto *elem: te->elements) print_expr(p, elem);
                    p->depth--;
                    break;
                }
            }
        }

        // ---------------------------------------------------------------------------
        // Statements
        // ---------------------------------------------------------------------------

        void print_block(Printer *p, const BlockStmt *b) {
            if (!b) {
                line(p, "<empty block>");
                return;
            }
            line(p, "Block_Stmt ({} stmts)", b->stmts.size());
            p->depth++;
            for (auto *s: b->stmts) print_stmt(p, s);
            p->depth--;
        }

        void print_case_clause(Printer *p, const CaseClause *c) {
            if (!c) {
                line(p, "<nil case>");
                return;
            }
            line(p, "Case_Clause");
            p->depth++;
            if (!c->values.empty()) {
                indent(p);
                std::format_to(std::back_inserter(p->buf), "values ({}):\n", c->values.size());
                p->depth++;
                for (auto *v: c->values) print_expr(p, v);
                p->depth--;
            }
            if (!c->body.empty()) {
                indent(p);
                std::format_to(std::back_inserter(p->buf), "body ({} stmts):\n", c->body.size());
                p->depth++;
                for (auto *s: c->body) print_stmt(p, s);
                p->depth--;
            }
            p->depth--;
        }

        void print_stmt(Printer *p, const Stmt *s) {
            if (!s) {
                line(p, "<nil>");
                return;
            }
            switch (s->kind) {
                case StmtKind::Expr: {
                    auto *es = static_cast<const ExprStmt *>(s);
                    line(p, "Expr_Stmt");
                    p->depth++;
                    print_expr(p, es->expr);
                    p->depth--;
                    break;
                }
                case StmtKind::Var: {
                    auto *vs = static_cast<const VarStmt *>(s);
                    indent(p);
                    p->buf += "Var_Stmt ";
                    p->buf += vs->name.text;
                    if (vs->is_mutable) p->buf += " [mutable]";
                    p->buf += '\n';
                    p->depth++;
                    if (vs->type_) {
                        indent(p);
                        p->buf += "type: ";
                        write_type(p, vs->type_);
                        p->buf += '\n';
                    }
                    if (vs->init) print_sub_expr(p, "init", vs->init);
                    p->depth--;
                    break;
                }
                case StmtKind::Block:
                    print_block(p, static_cast<const BlockStmt *>(s));
                    break;
                case StmtKind::If: {
                    auto *is = static_cast<const IfStmt *>(s);
                    line(p, "If_Stmt");
                    p->depth++;
                    print_sub_expr(p, "condition", is->condition);
                    indent(p);
                    p->buf += "then:\n";
                    p->depth++;
                    print_block(p, is->then_branch);
                    p->depth--;
                    if (is->else_branch) {
                        indent(p);
                        p->buf += "else:\n";
                        p->depth++;
                        print_stmt(p, is->else_branch);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case StmtKind::While: {
                    auto *ws = static_cast<const WhileStmt *>(s);
                    line(p, "While_Stmt");
                    p->depth++;
                    print_sub_expr(p, "condition", ws->condition);
                    indent(p);
                    p->buf += "body:\n";
                    p->depth++;
                    print_block(p, ws->body);
                    p->depth--;
                    p->depth--;
                    break;
                }
                case StmtKind::For: {
                    auto *fs = static_cast<const ForStmt *>(s);
                    line(p, "For_Stmt");
                    p->depth++;
                    if (fs->init) {
                        indent(p);
                        p->buf += "init:\n";
                        p->depth++;
                        print_stmt(p, fs->init);
                        p->depth--;
                    }
                    if (fs->condition) print_sub_expr(p, "condition", fs->condition);
                    if (fs->update) print_sub_expr(p, "update", fs->update);
                    indent(p);
                    p->buf += "body:\n";
                    p->depth++;
                    print_block(p, fs->body);
                    p->depth--;
                    p->depth--;
                    break;
                }
                case StmtKind::ForIn: {
                    auto *fi = static_cast<const ForInStmt *>(s);
                    line(p, "For_In_Stmt var={}", fi->variable.text);
                    p->depth++;
                    print_sub_expr(p, "iterable", fi->iterable);
                    indent(p);
                    p->buf += "body:\n";
                    p->depth++;
                    print_block(p, fi->body);
                    p->depth--;
                    p->depth--;
                    break;
                }
                case StmtKind::Switch: {
                    auto *sw = static_cast<const SwitchStmt *>(s);
                    line(p, "Switch_Stmt");
                    p->depth++;
                    print_sub_expr(p, "subject", sw->subject);
                    if (!sw->cases.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf), "cases ({}):\n", sw->cases.size());
                        p->depth++;
                        for (auto *c: sw->cases) print_case_clause(p, c);
                        p->depth--;
                    }
                    if (!sw->default_body.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "default ({} stmts):\n", sw->default_body.size());
                        p->depth++;
                        for (auto *stmt: sw->default_body) print_stmt(p, stmt);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case StmtKind::Break: line(p, "Break_Stmt");
                    break;
                case StmtKind::Continue: line(p, "Continue_Stmt");
                    break;
                case StmtKind::Return: {
                    auto *rs = static_cast<const ReturnStmt *>(s);
                    line(p, "Return_Stmt");
                    if (rs->value) {
                        p->depth++;
                        print_expr(p, rs->value);
                        p->depth--;
                    }
                    break;
                }
                case StmtKind::Defer: {
                    auto* ds = static_cast<const ast::DeferStmt*>(s);
                    line(p, "Defer_Stmt");
                    p->depth++;
                    print_stmt(p, ds->body);
                    p->depth--;
                    break;
                }
                case StmtKind::Panic: {
                    auto *ps = static_cast<const ast::PanicStmt *>(s);
                    line(p, "Panic_Stmt");
                    p->depth++;
                    print_expr(p, ps->message);
                    p->depth--;
                    break;
                }
            }
        }

        // ---------------------------------------------------------------------------
        // Declarations
        // ---------------------------------------------------------------------------

        void print_fn_decl(Printer *p, const FnDecl *fn) {
            if (!fn) {
                line(p, "<nil fn>");
                return;
            }
            indent(p);
            p->buf += "Fn_Decl ";
            p->buf += fn->name.text;
            write_modifiers(p, fn->visibility, fn->is_static, fn->is_final,
                            fn->is_abstract, fn->is_override);
            p->buf += '\n';
            p->depth++;

            if (!fn->type_params.empty()) {
                indent(p);
                std::format_to(std::back_inserter(p->buf),
                               "type_params ({}):\n", fn->type_params.size());
                p->depth++;
                for (auto *tp: fn->type_params) print_type_param(p, tp);
                p->depth--;
            }
            if (!fn->params.empty()) {
                indent(p);
                std::format_to(std::back_inserter(p->buf),
                               "params ({}):\n", fn->params.size());
                p->depth++;
                for (auto *param: fn->params) print_param(p, param);
                p->depth--;
            }
            if (fn->return_type) {
                indent(p);
                p->buf += "return: ";
                write_type(p, fn->return_type);
                p->buf += '\n';
            }
            if (fn->body) {
                indent(p);
                p->buf += "body:\n";
                p->depth++;
                print_block(p, fn->body);
                p->depth--;
            } else {
                line(p, "<no body>");
            }
            p->depth--;
        }

        void print_field(Printer *p, const FieldDecl *f) {
            if (!f) {
                line(p, "<nil field>");
                return;
            }
            indent(p);
            std::format_to(std::back_inserter(p->buf), "Field_Decl {}", f->name.text);
            write_modifiers(p, f->visibility, f->is_static, f->is_final, false, false);
            if (f->type_) {
                p->buf += ": ";
                write_type(p, f->type_);
            }
            if (f->init) {
                p->buf += " = ";
                write_inline_expr(p, f->init);
            }
            p->buf += '\n';
        }

        void print_decl(Printer *p, const Decl *d) {
            if (!d) {
                line(p, "<nil decl>");
                return;
            }
            switch (d->kind) {
                case DeclKind::Import: {
                    auto *im = static_cast<const ImportDecl *>(d);
                    indent(p);
                    p->buf += "Import_Decl \"";
                    write_escaped(p, im->path.text);
                    p->buf += '"';
                    if (im->has_alias) {
                        p->buf += " as ";
                        p->buf += im->alias.text;
                    }
                    p->buf += '\n';
                    break;
                }
                case DeclKind::Fn:
                    print_fn_decl(p, static_cast<const FnDecl *>(d));
                    break;
                case DeclKind::Class: {
                    auto *cd = static_cast<const ClassDecl *>(d);
                    indent(p);
                    p->buf += "Class_Decl ";
                    p->buf += cd->name.text;
                    write_modifiers(p, cd->visibility, false, cd->is_final, cd->is_abstract, false);
                    p->buf += '\n';
                    p->depth++;

                    if (!cd->type_params.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "type_params ({}):\n", cd->type_params.size());
                        p->depth++;
                        for (auto *tp: cd->type_params) print_type_param(p, tp);
                        p->depth--;
                    }
                    if (cd->base) {
                        indent(p);
                        p->buf += "extends: ";
                        write_type(p, cd->base);
                        p->buf += '\n';
                    }
                    if (!cd->interfaces.empty()) {
                        indent(p);
                        p->buf += "implements: ";
                        for (std::size_t i = 0; i < cd->interfaces.size(); ++i) {
                            if (i > 0) p->buf += ", ";
                            write_type(p, cd->interfaces[i]);
                        }
                        p->buf += '\n';
                    }
                    if (!cd->fields.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "fields ({}):\n", cd->fields.size());
                        p->depth++;
                        for (auto *f: cd->fields) print_field(p, f);
                        p->depth--;
                    }
                    if (!cd->methods.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "methods ({}):\n", cd->methods.size());
                        p->depth++;
                        for (auto *m: cd->methods) print_fn_decl(p, m);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case DeclKind::Interface: {
                    auto *id = static_cast<const InterfaceDecl *>(d);
                    indent(p);
                    p->buf += "Interface_Decl ";
                    p->buf += id->name.text;
                    write_modifiers(p, id->visibility, false, false, false, false);
                    p->buf += '\n';
                    p->depth++;

                    if (!id->type_params.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "type_params ({}):\n", id->type_params.size());
                        p->depth++;
                        for (auto *tp: id->type_params) print_type_param(p, tp);
                        p->depth--;
                    }
                    if (!id->bases.empty()) {
                        indent(p);
                        p->buf += "extends: ";
                        for (std::size_t i = 0; i < id->bases.size(); ++i) {
                            if (i > 0) p->buf += ", ";
                            write_type(p, id->bases[i]);
                        }
                        p->buf += '\n';
                    }
                    if (!id->methods.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "methods ({}):\n", id->methods.size());
                        p->depth++;
                        for (auto *m: id->methods) print_fn_decl(p, m);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case DeclKind::Enum: {
                    auto *ed = static_cast<const EnumDecl *>(d);
                    indent(p);
                    p->buf += "Enum_Decl ";
                    p->buf += ed->name.text;
                    write_modifiers(p, ed->visibility, false, false, false, false);
                    p->buf += '\n';
                    p->depth++;
                    if (!ed->variants.empty()) {
                        indent(p);
                        std::format_to(std::back_inserter(p->buf),
                                       "variants ({}):\n", ed->variants.size());
                        p->depth++;
                        for (auto *v: ed->variants) print_variant(p, v);
                        p->depth--;
                    }
                    p->depth--;
                    break;
                }
                case DeclKind::Var: {
                    auto *vd = static_cast<const VarDecl *>(d);
                    indent(p);
                    p->buf += "Var_Decl ";
                    p->buf += vd->name.text;
                    if (vd->is_mutable) p->buf += " [mutable]";
                    write_modifiers(p, vd->visibility, false, false, false, false);
                    p->buf += '\n';
                    p->depth++;
                    if (vd->type_) {
                        indent(p);
                        p->buf += "type: ";
                        write_type(p, vd->type_);
                        p->buf += '\n';
                    }
                    if (vd->init) print_sub_expr(p, "init", vd->init);
                    p->depth--;
                    break;
                }
            }
        }

        void print_program(Printer *p, const Program &prog) {
            line(p, "Program ({} decls)", prog.decls.size());
            p->depth++;
            for (auto *d: prog.decls) print_decl(p, d);
            p->depth--;
        }
    } // namespace

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    Printer make_printer(std::pmr::memory_resource *mr) {
        return Printer{
            .buf = std::pmr::string{mr},
            .depth = 0,
        };
    }

    void dump_program(Printer *p, const Program &prog) { print_program(p, prog); }
    void dump_decl(Printer *p, const Decl *d) { print_decl(p, d); }
    void dump_stmt(Printer *p, const Stmt *s) { print_stmt(p, s); }
    void dump_expr(Printer *p, const Expr *e) { print_expr(p, e); }

    void dump_type(Printer *p, const Type *t) {
        indent(p);
        write_type(p, t);
        p->buf += '\n';
    }
} // namespace frappe::ast_printer
