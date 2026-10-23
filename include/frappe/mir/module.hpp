//
// Created by antar on 21/10/26.
//

#pragma once

#include <memory_resource>
#include <vector>

#include <frappe/mir/class.hpp>
#include <frappe/mir/function.hpp>

namespace frappe::mir {
    struct MirModule {
        std::pmr::vector<MirFunction> functions;
        std::pmr::vector<MirClass> classes;
    };
}