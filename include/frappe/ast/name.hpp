//
// Created by antar on 19/10/26.
//

#pragma once
#include <string_view>
#include <frappe/source/span.hpp>


namespace frappe::ast {
    struct Name {
        std::string_view text;
        source::Span span;
    };
}