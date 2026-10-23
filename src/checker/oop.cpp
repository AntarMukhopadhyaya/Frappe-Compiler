//
// Created by antar on 19/10/26.
//

#include <cstring>
#include <frappe/checker/checker.hpp>

#include <string_view>

#include <frappe/ast/decl.hpp>
#include <frappe/ast/expr.hpp>
#include <frappe/fatal/fatality.hpp>
#include <frappe/syntax/constants.hpp>

#include "checker_internal.hpp"


namespace frappe::checker::detail {
    namespace {
        using types::Type;
        using types::TypeKind;

        Type *bind_expr(Checker *c, const void *node, Type *t) {
            set_type(c, node, t);
            return t;
        }
    }

    const ast::ClassDecl *get_base_class(Checker *c, const ast::ClassDecl *cd) {
        if (!cd->base) return nullptr;
        auto *ty = resolve_ast_type(c, cd->base);
        if (!ty || ty->kind != TypeKind::Named) return nullptr;
        auto *nt = static_cast<const types::NamedType *>(ty);
        if (nt->decl.kind != types::NamedDeclKind::Class) return nullptr;
        return nt->decl.class_;
    }

    const ast::FieldDecl *find_field(Checker *c, const ast::ClassDecl *start,
                                     std::string_view name) {
        const ast::ClassDecl *cur = start;
        while (cur) {
            for (auto *f: cur->fields) {
                if (f->name.text == name) return f;
            }
            cur = get_base_class(c, cur);
        }
        return nullptr;
    }

    const ast::FnDecl *find_method(Checker *c, const ast::ClassDecl *start,
                                   std::string_view name) {
        const ast::ClassDecl *cur = start;
        while (cur) {
            for (auto *m: cur->methods) {
                if (m->name.text == name) return m;
            }
            cur = get_base_class(c, cur);
        }
        return nullptr;
    }

    // Rule 4: final fields can only be assigned in the declaring class's init
    void check_field_assign(Checker *c, const ast::MemberExpr *me) {
        auto *obj_ty = check_expr(c, me->object);
        if (!obj_ty || types::is_error(obj_ty)) return;
        if (obj_ty->kind != TypeKind::Named) return;

        auto *nt = static_cast<const types::NamedType *>(obj_ty);
        if (nt->decl.kind != types::NamedDeclKind::Class) return;

        auto hit = find_field_with_declaring(c, nt->decl.class_, me->name.text);
        if (!hit.field) return;
        if (!check_member_visibility(c, me, hit.declaring, hit.field->visibility, "field")) return;
        if (!hit.field->is_final) return;

        if (c->current_class != hit.declaring) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "cannot assign to final field '{}' outside declaring class '{}'",
                                      me->name.text, hit.declaring->name.text);
            return;
        }
        if (!c->current_fn
            || c->current_fn->name.text != frappe::syntax::INIT_NAME) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "final field '{}' can only be assigned in 'init'", me->name.text);
        }
    }


    Type *check_this(Checker *c, const ast::ThisExpr *te) {
        if (!c->current_class) {
            diagnostics::report_error(c->diag, te->span,
                                      "'this' is only valid inside a method");
            return bind_expr(c, te, c->store.error_type);
        }
        if (c->current_fn && c->current_fn->is_static) {
            diagnostics::report_error(c->diag, te->span,
                                      "'this' is not valid in a static method");
            return bind_expr(c, te, c->store.error_type);
        }
        return bind_expr(c, te, types::named_type(&c->store, types::NamedTypeDeclRef{
                                                      .kind = types::NamedDeclKind::Class,
                                                      .class_ = c->current_class,
                                                  }));
    }

    Type *check_super(Checker *c, const ast::SuperExpr *se) {
        if (!c->current_class) {
            diagnostics::report_error(c->diag, se->span,
                                      "'super' is only valid inside a method");
            return bind_expr(c, se, c->store.error_type);
        }
        auto *base = get_base_class(c, c->current_class);
        if (!base) {
            diagnostics::report_error(c->diag, se->span,
                                      "'super' requires a class with a base class");
            return bind_expr(c, se, c->store.error_type);
        }
        return bind_expr(c, se, types::named_type(&c->store, types::NamedTypeDeclRef{
                                                      .kind = types::NamedDeclKind::Class,
                                                      .class_ = base,
                                                  }));
    }

    Type *check_member(Checker *c, const ast::MemberExpr *me) {
        auto *obj_ty = check_expr(c, me->object);
        if (types::is_error(obj_ty)) return bind_expr(c, me, c->store.error_type);
        if (obj_ty->kind != TypeKind::Named) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "cannot access '{}' on non-class type {}",
                                      me->name.text, types::type_name(c->store.mr, obj_ty));
            return bind_expr(c, me, c->store.error_type);
        }
        auto *nt = static_cast<const types::NamedType *>(obj_ty);
        if (nt->decl.kind != types::NamedDeclKind::Class) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "cannot access '{}' on interface or enum; only classes have members",
                                      me->name.text);
            return bind_expr(c, me, c->store.error_type);
        }

        auto *cd = nt->decl.class_;
        if (auto hit = find_field_with_declaring(c, cd, me->name.text); hit.field) {
            if (!check_member_visibility(c, me, hit.declaring, hit.field->visibility, "field")) {
                return bind_expr(c, me, c->store.error_type);
            }
            if (auto *sym = resolver::lookup(c->resolver_, hit.field); sym) {
                return bind_expr(c, me, type_of_symbol(c, sym));
            }
            return bind_expr(c, me, c->store.error_type);
        }

        if (find_method(c, cd, me->name.text)) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "method '{}' must be called", me->name.text);
            return bind_expr(c, me, c->store.error_type);
        }

        diagnostics::report_error(c->diag, me->name.span,
                                  "class '{}' has no member '{}'", cd->name.text, me->name.text);
        return bind_expr(c, me, c->store.error_type);
    }

    // Resolves a call of the form ClassName.method(...). Called from
    // check_method_call when the receiver is a bare identifier naming a class.
    // Semantics: method must be declared 'static'; no 'this' receiver in args
    Type *check_static_call(Checker *c, const ast::CallExpr *call,
                            const ast::MemberExpr *me,
                            const resolver::Symbol *class_sym) {
        auto *cd = class_sym->decl.class_;
        if (!cd) {
            // Defensive: class_sym was .Class but has no ClassDecl pointer.
            // Should be impossible.
            FRAPPE_UNREACHABLE(me->span,
                               "class symbol '{}' has no ClassDecl", class_sym->name);
        }

        // One walk: finds the method AND the class that declares it. Matters
        // for inheritance — `Child.inherited_static()` resolves to
        // `Parent.inherited_static`, so `declaring` may differ from `cd`.
        auto hit = find_method_with_declaring(c, cd, me->name.text);

        if (!hit.method) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "class '{}' has no static method '{}'",
                                      cd->name.text, me->name.text);
            for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, call, c->store.error_type);
        }

        auto *method = hit.method;
        auto *declaring = hit.declaring;

        if (!method->is_static) {
            diagnostics::report_error(c->diag, call->span,
                                      "'{}.{}' is an instance method — call it on an object, not the class",
                                      cd->name.text, me->name.text);
            for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, call, c->store.error_type);
        }

        std::pmr::string callee{c->store.mr};
        callee += declaring->name.text;
        callee += '.';
        callee += method->name.text;
        record_call_name(c, call, std::move(callee));

        if (!check_member_visibility(c, me, declaring, method->visibility, "static method")) {
            for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, call, c->store.error_type);
        }

        if (call->args.size() != method->params.size()) {
            diagnostics::report_error(c->diag, call->span,
                                      "'{}.{}' expects {} arguments, got {}",
                                      declaring->name.text, me->name.text,
                                      method->params.size(), call->args.size());
            for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, call, resolve_ast_type(c, method->return_type));
        }

        for (std::size_t i = 0; i < call->args.size(); ++i) {
            auto *at = check_expr(c, call->args[i]);
            auto *pt = resolve_ast_type(c, method->params[i]->type_);
            check_assignable(c, pt, at, call->args[i]->span);
        }

        return bind_expr(c, call, resolve_ast_type(c, method->return_type));
    }

    Type *check_method_call(Checker *c, const ast::CallExpr *call,
                            const ast::MemberExpr *me) {
        // Static call: `ClassName.method(...)`. Receiver is an Ident whose
        // symbol resolves to a Class — not a value expression.
        if (me->object->kind == ast::ExprKind::Ident) {
            auto *ie = static_cast<const ast::IdentExpr *>(me->object);
            if (auto *sym = resolver::lookup(c->resolver_, ie);
                sym && sym->kind == resolver::SymbolKind::Class) {
                return check_static_call(c, call, me, sym);
            }
        }

        auto *obj_ty = check_expr(c, me->object);

        if (types::is_error(obj_ty)) {
            for (auto *arg: call->args) {
                static_cast<void>(check_expr(c, arg));
            }
            return bind_expr(c, call, c->store.error_type);
        }

        if (obj_ty->kind != TypeKind::Named) {
            diagnostics::report_error(
                c->diag, call->span,
                "cannot call method on non-class type {}",
                types::type_name(c->store.mr, obj_ty));

            for (auto *arg: call->args) {
                static_cast<void>(check_expr(c, arg));
            }
            return bind_expr(c, call, c->store.error_type);
        }

        auto *nt = static_cast<const types::NamedType *>(obj_ty);

        if (nt->decl.kind != types::NamedDeclKind::Class) {
            diagnostics::report_error(
                c->diag, call->span,
                "method call requires a class");

            for (auto *arg: call->args) {
                static_cast<void>(check_expr(c, arg));
            }
            return bind_expr(c, call, c->store.error_type);
        }

        auto *cd = nt->decl.class_;

        // One walk: finds the method AND the class that declares it.
        auto hit = find_method_with_declaring(c, cd, me->name.text);

        if (!hit.method) {
            // Distinguish "field used as method" from "no member at all".
            if (find_field(c, cd, me->name.text)) {
                diagnostics::report_error(
                    c->diag, me->name.span,
                    "'{}' is a field, not a method",
                    me->name.text);
            } else {
                diagnostics::report_error(
                    c->diag, me->name.span,
                    "class '{}' has no method '{}'",
                    cd->name.text, me->name.text);
            }

            for (auto *arg: call->args) {
                static_cast<void>(check_expr(c, arg));
            }
            return bind_expr(c, call, c->store.error_type);
        }

        auto *method = hit.method;
        auto *declaring = hit.declaring;

        // Store the fully qualified method name in long-lived memory.
        std::pmr::string callee{c->store.mr};
        callee += declaring->name.text;
        callee += '.';
        callee += method->name.text;

        record_call_name(c, call, std::move(callee));

        if (method->name.text == syntax::INIT_NAME) {
            // Only super.init(...) is allowed for constructor delegation.
            if (me->object->kind != ast::ExprKind::Super) {
                diagnostics::report_error(
                    c->diag, call->span,
                    "use 'new Class(...)' to construct, not '.init(...)'");

                for (auto *arg: call->args) {
                    static_cast<void>(check_expr(c, arg));
                }
                return bind_expr(c, call, c->store.error_type);
            }
        }

        if (!check_member_visibility(c, me, declaring, method->visibility, "method")) {
            for (auto *arg: call->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, call, c->store.error_type);
        }

        if (call->args.size() != method->params.size()) {
            diagnostics::report_error(
                c->diag, call->span,
                "'{}' expects {} arguments, got {}",
                me->name.text, method->params.size(), call->args.size());

            for (auto *arg: call->args) {
                static_cast<void>(check_expr(c, arg));
            }
            return bind_expr(
                c, call, resolve_ast_type(c, method->return_type));
        }

        for (std::size_t i = 0; i < call->args.size(); ++i) {
            auto *arg_type = check_expr(c, call->args[i]);
            auto *param_type = resolve_ast_type(c, method->params[i]->type_);

            check_assignable(c, param_type, arg_type, call->args[i]->span);
        }

        return bind_expr(c, call, resolve_ast_type(c, method->return_type));
    }

    Type *check_new(Checker *c, const ast::NewExpr *ne) {
        auto *ty = resolve_ast_type(c, ne->type_);

        if (types::is_error(ty)) {
            for (auto *arg: ne->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, ne, ty);
        }

        if (ty->kind != TypeKind::Named) {
            diagnostics::report_error(c->diag, ne->span, "'new' requires a class type");
            for (auto *arg: ne->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, ne, c->store.error_type);
        }

        auto *nt = static_cast<const types::NamedType *>(ty);
        if (nt->decl.kind != types::NamedDeclKind::Class) {
            diagnostics::report_error(c->diag, ne->span,
                                      "'new' requires a class, not an interface or enum");
            for (auto *arg: ne->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, ne, c->store.error_type);
        }

        auto *cd = nt->decl.class_;
        if (cd->is_abstract) {
            diagnostics::report_error(c->diag, ne->span,
                                      "cannot instantiate abstract class '{}'", cd->name.text);
            for (auto *arg: ne->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, ne, c->store.error_type);
        }
        auto *init = find_method(c, cd, "init");
        if (!init) {
            if (!ne->args.empty()) {
                diagnostics::report_error(c->diag, ne->span,
                                          "class '{}' has no constructor 'init'", cd->name.text);
            }
            for (auto *arg: ne->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, ne, ty);
        }

        if (ne->args.size() != init->params.size()) {
            diagnostics::report_error(c->diag, ne->span,
                                      "'{}.init' expects {} arguments, got {}",
                                      cd->name.text, init->params.size(), ne->args.size());
            for (auto *arg: ne->args) static_cast<void>(check_expr(c, arg));
            return bind_expr(c, ne, ty);
        }

        for (std::size_t i = 0; i < ne->args.size(); ++i) {
            auto *at = check_expr(c, ne->args[i]);
            auto *pt = resolve_ast_type(c, init->params[i]->type_);
            check_assignable(c, pt, at, ne->args[i]->span);
        }
        return bind_expr(c, ne, ty);
    }

    [[nodiscard]] bool is_subclass(Checker *c,
                                   const ast::ClassDecl *derived,
                                   const ast::ClassDecl *base) {
        if (derived == nullptr) return false;
        if (derived == base) return true;
        const ast::ClassDecl *walk = derived;
        while (walk) {
            walk = get_base_class(c, walk);
            if (walk == base) return true;
        }
        return false;
    }

    // Enforces visibility on a member access. Returns true if the access is legal.
    // Fires exactly one error at `me->name.span` on failure. `kind` is a
    // human-readable label ("field", "method", "static method") used in the message.
    [[nodiscard]] bool check_member_visibility(Checker *c,
                                               const ast::MemberExpr *me,
                                               const ast::ClassDecl *declaring,
                                               ast::Visibility vis,
                                               std::string_view kind) {
        if (vis == ast::Visibility::Default || vis == ast::Visibility::Public) {
            return true;
        }

        if (vis == ast::Visibility::Private) {
            if (c->current_class != declaring) {
                diagnostics::report_error(c->diag, me->name.span,
                                          "cannot access private {} '{}' — declared in class '{}'",
                                          kind, me->name.text, declaring->name.text);
                return false;
            }
            return true;
        }

        // Protected.
        if (!is_subclass(c, c->current_class, declaring)) {
            diagnostics::report_error(c->diag, me->name.span,
                                      "cannot access protected {} '{}' — declared in class '{}' "
                                      "(only subclasses may access)",
                                      kind, me->name.text, declaring->name.text);
            return false;
        }
        return true;
    }

    [[nodiscard]] Field_Hit find_field_with_declaring(
        Checker *c, const ast::ClassDecl *start, std::string_view name) {
        const ast::ClassDecl *walk = start;
        while (walk) {
            for (auto *f: walk->fields) {
                if (f->name.text == name) return {walk, f};
            }
            walk = get_base_class(c, walk);
        }
        return {};
    }

    [[nodiscard]] Method_Hit find_method_with_declaring(
        Checker *c, const ast::ClassDecl *start, std::string_view name) {
        const ast::ClassDecl *walk = start;
        while (walk) {
            for (auto *m: walk->methods) {
                if (m->name.text == name) return {walk, m};
            }
            walk = get_base_class(c, walk);
        }
        return {};
    }
}
