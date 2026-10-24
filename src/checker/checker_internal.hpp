//
// Created by antar on 19/10/26.
//

#pragma once

#include <format>
#include <iterator>
#include <frappe/checker/checker.hpp>
#include <frappe/source/span.hpp>
#include <ranges>

namespace frappe::checker::detail {
    [[nodiscard]] types::Type *resolve_ast_type(Checker *c, const ast::Type *t);

    [[nodiscard]] types::Type *symbol_to_type(Checker *c,
                                              const resolver::Symbol *sym,
                                              const source::Span &span);

    bool check_assignable(const Checker *c,
                          const types::Type *expected,
                          const types::Type *actual,
                          const source::Span &span);

    [[nodiscard]] types::Type *type_of_symbol(Checker *c,
                                              const resolver::Symbol *sym);

    [[nodiscard]] types::Type *check_expr(Checker *c, const ast::Expr *e);

    [[nodiscard]] types::Type *check_expr_against(Checker *c, const ast::Expr *e,
                                                  types::Type *expected);

    void check_stmt(Checker *c, const ast::Stmt *s);

    void check_decl(Checker *c, const ast::Decl *d);

    [[nodiscard]] types::Type *check_this(Checker *c, const ast::ThisExpr *te);

    [[nodiscard]] types::Type *check_super(Checker *c, const ast::SuperExpr *se);

    [[nodiscard]] types::Type *check_member(Checker *c, const ast::MemberExpr *me);

    [[nodiscard]] types::Type *check_method_call(Checker *c, const ast::CallExpr *call,
                                                 const ast::MemberExpr *me);

    [[nodiscard]] types::Type *check_new(Checker *c, const ast::NewExpr *ne);

    void check_fn_decl(Checker *c, const ast::FnDecl *fn);

    [[nodiscard]] types::Type *check_generic_call(Checker *c,
                                                  const ast::CallExpr *call,
                                                  const ast::FnDecl *fn);

    void drain_worklist(Checker *c);





    [[nodiscard]] const ast::ClassDecl *get_base_class(const Checker *c, const ast::ClassDecl *cd);

    [[nodiscard]] const ast::FieldDecl *find_field(Checker *c, const ast::ClassDecl *start, std::string_view name);

    [[nodiscard]] const ast::FnDecl *find_method(Checker *c, const ast::ClassDecl *start, std::string_view name);

    void check_field_assign(Checker *c, const ast::MemberExpr *me);

    [[nodiscard]] bool check_member_visibility(Checker *c,
                                               const ast::MemberExpr *me,
                                               const ast::ClassDecl *declaring,
                                               ast::Visibility vis,
                                               std::string_view kind);

    [[nodiscard]] bool is_subclass(const Checker *c,
                                   const ast::ClassDecl *derived,
                                   const ast::ClassDecl *base);

    struct Field_Hit {
        const ast::ClassDecl *declaring;
        const ast::FieldDecl *field;
    };

    struct Method_Hit {
        const ast::ClassDecl *declaring;
        const ast::FnDecl *method;
    };

    [[nodiscard]] Field_Hit find_field_with_declaring(Checker *c, const ast::ClassDecl *start, std::string_view name);

    [[nodiscard]] Method_Hit find_method_with_declaring(Checker *c, const ast::ClassDecl *start, std::string_view name);
}
