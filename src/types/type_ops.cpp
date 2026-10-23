//
// Created by antar on 19/10/26.
//

#include <frappe/types/types.hpp>

#include <format>

namespace frappe::types {
    bool type_eq(const Type *a, const Type *b) {
        if (a == b) return true;
        if (!a || !b) return false;
        if (a->kind != b->kind) return false;

        switch (a->kind) {
            case TypeKind::Builtin: {
                auto *ba = static_cast<const BuiltinType *>(a);
                auto *bb = static_cast<const BuiltinType *>(b);
                return ba->b == bb->b;
            }
            case TypeKind::Named: {
                auto *na = static_cast<const NamedType *>(a);
                auto *nb = static_cast<const NamedType *>(b);
                if (na->args.size() != nb->args.size()) return false;
                if (named_decl_ptr(na->decl) != named_decl_ptr(nb->decl)) return false;
                for (std::size_t i = 0; i < na->args.size(); ++i) {
                    if (!type_eq(na->args[i], nb->args[i])) return false;
                }
                return true;
            }
            case TypeKind::Array: {
                auto *aa = static_cast<const ArrayType *>(a);
                auto *ab = static_cast<const ArrayType *>(b);
                return aa->size == ab->size && type_eq(aa->element, ab->element);
            }
            case TypeKind::Optional: {
                auto *oa = static_cast<const OptionalType *>(a);
                auto *ob = static_cast<const OptionalType *>(b);
                return type_eq(oa->inner, ob->inner);
            }
            case TypeKind::Func: {
                auto *fa = static_cast<const FuncType *>(a);
                auto *fb = static_cast<const FuncType *>(b);
                if (fa->params.size() != fb->params.size()) return false;
                for (std::size_t i = 0; i < fa->params.size(); ++i) {
                    if (!type_eq(fa->params[i], fb->params[i])) return false;
                }
                return type_eq(fa->ret, fb->ret);
            }
            case TypeKind::Tuple: {
                auto *ta = static_cast<const TupleType *>(a);
                auto *tb = static_cast<const TupleType *>(b);
                if (ta->elems.size() != tb->elems.size()) return false;
                for (std::size_t i = 0; i < ta->elems.size(); ++i) {
                    if (!type_eq(ta->elems[i], tb->elems[i])) return false;
                }
                return true;
            }
            case TypeKind::TypeVar: {
                auto *va = static_cast<const TypeVar *>(a);
                auto *vb = static_cast<const TypeVar *>(b);
                return va->name == vb->name && va->owner == vb->owner;
            }
            case TypeKind::Unknown: return true;
            case TypeKind::Error: return true;
        }
        return false;
    }

    void write_type_name(std::pmr::string &out, const Type *t) {
        if (!t) {
            out += "<nil>";
            return;
        }
        switch (t->kind) {
            case TypeKind::Builtin: {
                auto *b = static_cast<const BuiltinType *>(t);
                out += resolver::BUILTIN_NAMES[static_cast<std::size_t>(b->b)];
                break;
            }
            case TypeKind::Named: {
                auto *n = static_cast<const NamedType *>(t);
                out += named_decl_name(n->decl);
                if (!n->args.empty()) {
                    out += '<';
                    for (std::size_t i = 0; i < n->args.size(); ++i) {
                        if (i > 0) out += ", ";
                        write_type_name(out, n->args[i]);
                    }
                    out += '>';
                }
                break;
            }
            case TypeKind::Array: {
                auto *a = static_cast<const ArrayType *>(t);
                out += '[';
                write_type_name(out, a->element);
                if (a->size < 0) out += "; dynamic]";
                else std::format_to(std::back_inserter(out), "; {}]", a->size);
                break;
            }
            case TypeKind::Optional: {
                auto *o = static_cast<const OptionalType *>(t);
                write_type_name(out, o->inner);
                out += '?';
                break;
            }
            case TypeKind::Func: {
                auto *f = static_cast<const FuncType *>(t);
                out += '(';
                for (std::size_t i = 0; i < f->params.size(); ++i) {
                    if (i > 0) out += ", ";
                    write_type_name(out, f->params[i]);
                }
                out += ") -> ";
                write_type_name(out, f->ret);
                break;
            }
            case TypeKind::Tuple: {
                auto *tt = static_cast<const TupleType *>(t);
                out += '(';
                for (std::size_t i = 0; i < tt->elems.size(); ++i) {
                    if (i > 0) out += ", ";
                    write_type_name(out, tt->elems[i]);
                }
                out += ')';
                break;
            }
            case TypeKind::TypeVar: {
                auto *v = static_cast<const TypeVar *>(t);
                out += v->name;
                break;
            }
            case TypeKind::Unknown: out += "<unknown>";
                break;
            case TypeKind::Error: out += "<error>";
                break;
        }
    }

    std::pmr::string type_name(std::pmr::memory_resource *mr, const Type *t) {
        std::pmr::string s{mr};
        write_type_name(s, t);
        return s;
    }
}
