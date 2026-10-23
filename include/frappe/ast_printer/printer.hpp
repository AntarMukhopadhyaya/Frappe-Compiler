//
// Created by antar on 19/10/26.
//

#pragma  once

#include <memory_resource>
#include <string>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/program.hpp>
#include <frappe/ast/stmt.hpp>
#include <frappe/ast/type.hpp>

namespace frappe::ast_printer {
    struct Printer {
        std::pmr::string buf;
        int              depth = 0;
    };

    [[nodiscard]] Printer make_printer(std::pmr::memory_resource* mr);

    void dump_program(Printer* p, const ast::Program& prog);
    void dump_decl   (Printer* p, const ast::Decl*    d);
    void dump_stmt   (Printer* p, const ast::Stmt*    s);
    void dump_expr   (Printer* p, const ast::Expr*    e);
    void dump_type   (Printer* p, const ast::Type*    t);
}