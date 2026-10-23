//
// Created by antar on 19/10/26.
//

#pragma once

#include <memory_resource>
#include <vector>

#include <frappe/ast/node.hpp>
#include <frappe/ast/name.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/ast/type.hpp>

namespace frappe::ast {
    struct BlockStmt;

    struct ExprStmt : Stmt {
        Expr *expr = nullptr;
    };

    struct VarStmt : Stmt {
        Name name;
        Type *type_ = nullptr; // nil → inferred
        Expr *init = nullptr; // nil → uninitialized
        bool is_mutable = false;
    };

    struct BlockStmt : Stmt {
        std::pmr::vector<Stmt *> stmts;
    };

    struct IfStmt : Stmt {
        Expr *condition = nullptr;
        BlockStmt *then_branch = nullptr;
        Stmt *else_branch = nullptr; // nil, ^BlockStmt, or ^IfStmt
    };

    struct WhileStmt : Stmt {
        Expr *condition = nullptr;
        BlockStmt *body = nullptr;
    };

    struct ForStmt : Stmt {
        Stmt *init = nullptr;
        Expr *condition = nullptr;
        Expr *update = nullptr;
        BlockStmt *body = nullptr;
    };

    struct ForInStmt : Stmt {
        Name variable;
        Expr *iterable = nullptr;
        BlockStmt *body = nullptr;
    };

    struct CaseClause {
        source::Span span;
        std::pmr::vector<Expr *> values;
        std::pmr::vector<Stmt *> body;
    };

    struct SwitchStmt : Stmt {
        Expr *subject = nullptr;
        std::pmr::vector<CaseClause *> cases;
        std::pmr::vector<Stmt *> default_body; // empty = no default
    };

    struct BreakStmt : Stmt {
    };

    struct ContinueStmt : Stmt {
    };

    struct ReturnStmt : Stmt {
        Expr *value = nullptr;
    };
    struct DeferStmt : Stmt {
        Stmt* body = nullptr;
    };
    struct PanicStmt : Stmt {
        Expr* message = nullptr;
    };
}
