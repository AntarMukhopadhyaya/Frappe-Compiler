//
// Created by antar on 19/10/26.
//
#include <frappe/resolver/resolver.hpp>
#include <cassert>
#include <format>
#include <frappe/fatal/fatality.hpp>

namespace frappe::resolver {
    namespace {
        template<typename T>
        [[nodiscard]] T *allocate(Resolver *r) {
            void *mem = r->mr->allocate(sizeof(T), alignof(T));
            return new(mem) T{};
        }

        void install_builtin(Resolver *r) {
            for (std::size_t i = 0; i < BUILTIN_NAMES.size(); ++i) {
                Symbol *sym = allocate<Symbol>(r);
                sym->name = BUILTIN_NAMES[i];
                sym->kind = SymbolKind::BuiltinType;
                sym->builtin = static_cast<Builtin>(i);
                r->module_scope->symbols.emplace(BUILTIN_NAMES[i], sym);
            }
        }
    }

    Resolver make_resolver(diagnostics::Engine *diag, std::pmr::memory_resource *mr) {
        Resolver r{
            .mr = mr,
            .diag = diag,
            .bindings = std::pmr::unordered_map<const void *, Symbol *>{mr},
            .this_syms = std::pmr::unordered_map<const ast::FnDecl *, Symbol *>{mr},
            .super_syms = std::pmr::unordered_map<const ast::FnDecl *, Symbol *>{mr},
        };
        r.module_scope = push_scope(&r, ScopeKind::Module);
        install_builtin(&r);
        return r;
    }

    Scope *push_scope(Resolver *r, ScopeKind kind) {
        Scope *s = new(r->mr->allocate(sizeof(Scope), alignof(Scope)))
                Scope{
                    .parent = r->current,
                    .kind = kind,
                    .symbols = std::pmr::unordered_map<std::string_view, Symbol *>{r->mr},
                };
        r->current = s;
        return s;
    }

    void pop_scope(Resolver *r) {
        if (r->current == nullptr || r->current->parent == nullptr) {
            FRAPPE_UNREACHABLE({}, "pop_scope underflow — scope stack unbalanced");
        }
        r->current = r->current->parent;
    }

    Symbol *declare(Resolver *r, std::string_view name, SymbolKind kind,
                    DeclRef decl, source::Span span) {
        auto it = r->current->symbols.find(name);
        if (it != r->current->symbols.end()) {
            diagnostics::report_error(r->diag, span,
                                      "'{}' is already declared in this scope", name);
            return it->second;
        }
        auto *sym = allocate<Symbol>(r);
        sym->name = name;
        sym->kind = kind;
        sym->decl = decl;
        r->current->symbols.emplace(name, sym);
        return sym;
    }

    Symbol *declare_synthetic(Resolver *r, std::string_view name,
                              SymbolKind kind, Symbol *parent) {
        auto *sym = allocate<Symbol>(r);
        sym->name = name;
        sym->kind = kind;
        sym->parent = parent;
        r->current->symbols.emplace(name, sym);
        return sym;
    }

    Symbol *resolve(Resolver *r, std::string_view name) {
        Scope *scope = r->current;
        while (scope != nullptr) {
            auto it = scope->symbols.find(name);
            if (it != scope->symbols.end()) return it->second;
            scope = scope->parent;
        }
        return nullptr;
    }

    void bind(Resolver *r, const void *node, Symbol *sym) {
        r->bindings.insert_or_assign(node, sym);
    }

    Symbol *lookup(Resolver *r, const void *node) {
        auto it = r->bindings.find(node);
        return it != r->bindings.end() ? it->second : nullptr;
    }

    Symbol *class_base(Symbol *class_sym) {
        if (!class_sym) return nullptr;
        return class_sym->base;
    }
}
