//
// Created by antar on 19/10/26.
//

#pragma once

#include <memory_resource>
#include <vector>

#include <frappe/ast/node.hpp>
#include <frappe/ast/decl.hpp>

namespace frappe::ast {

    struct Program {
        source::Span                span;
        std::pmr::vector<Decl*>     decls;
    };

} // namespace frappe::ast
