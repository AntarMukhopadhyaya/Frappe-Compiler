//
// Created by antar on 19/10/26.
//

#include <frappe/types/store.hpp>

#include <type_traits>

namespace frappe::types {
    namespace {
        template<typename T>
        [[nodiscard]] T *allocate(Store *s, TypeKind kind) {
            static_assert(std::is_base_of_v<Type, T>);
            void *mem = s->mr->allocate(sizeof(T), alignof(T));
            auto *t = ::new(mem) T{};
            t->kind = kind;
            return t;
        }

        [[nodiscard]] Type *mk_builtin(Store *s, resolver::Builtin b) {
            auto *t = allocate<BuiltinType>(s, TypeKind::Builtin);
            t->b = b;
            return t;
        }

        template<typename T>
        [[nodiscard]] Type *mk_sentinel(Store *s, TypeKind kind) {
            return allocate<T>(s, kind);
        }
    }

    Store make_store(std::pmr::memory_resource *mr) {
        Store s{
            .mr = mr,
            .named_type_cache = std::pmr::unordered_map<const void *, Type *>{mr},
        };

        s.int_type = mk_builtin(&s, resolver::Builtin::Int);
        s.float_type = mk_builtin(&s, resolver::Builtin::Float);
        s.bool_type = mk_builtin(&s, resolver::Builtin::Bool);
        s.string_type = mk_builtin(&s, resolver::Builtin::String);
        s.char_type = mk_builtin(&s, resolver::Builtin::Char);
        s.nil_type = mk_builtin(&s, resolver::Builtin::Nil);
        s.void_type = mk_builtin(&s, resolver::Builtin::Void);
        s.int8_type = mk_builtin(&s, resolver::Builtin::Int8);
        s.int16_type = mk_builtin(&s, resolver::Builtin::Int16);
        s.int32_type = mk_builtin(&s, resolver::Builtin::Int32);
        s.int64_type = mk_builtin(&s, resolver::Builtin::Int64);
        s.uint8_type = mk_builtin(&s, resolver::Builtin::UInt8);
        s.uint16_type = mk_builtin(&s, resolver::Builtin::UInt16);
        s.uint32_type = mk_builtin(&s, resolver::Builtin::UInt32);
        s.uint64_type = mk_builtin(&s, resolver::Builtin::UInt64);
        s.float32_type = mk_builtin(&s, resolver::Builtin::Float32);
        s.float64_type = mk_builtin(&s, resolver::Builtin::Float64);

        s.unknown_type = mk_sentinel<Unknown>(&s, TypeKind::Unknown);
        s.error_type = mk_sentinel<Error>(&s, TypeKind::Error);

        return s;
    }

    Type *named_type(Store *s, NamedTypeDeclRef ref) {
        const void *key = named_decl_ptr(ref);
        auto it = s->named_type_cache.find(key);
        if (it != s->named_type_cache.end()) return it->second;

        void *mem = s->mr->allocate(sizeof(NamedType), alignof(NamedType));
        auto *t = ::new(mem) NamedType{
            {TypeKind::Named},
            ref,
            std::pmr::vector<Type *>{s->mr},
        };
        s->named_type_cache.emplace(key, t);
        return t;
    }

    Type *array_type(Store *s, Type *elem, std::int64_t size) {
        void *mem = s->mr->allocate(sizeof(ArrayType), alignof(ArrayType));
        return ::new(mem) ArrayType{{TypeKind::Array}, elem, size};
    }

    Type *optional_type(Store *s, Type *inner) {
        void *mem = s->mr->allocate(sizeof(OptionalType), alignof(OptionalType));
        return ::new(mem) OptionalType{{TypeKind::Optional}, inner};
    }

    Type *func_type(Store *s, std::span<Type * const> params, Type *ret) {
        void *mem = s->mr->allocate(sizeof(FuncType), alignof(FuncType));
        return ::new(mem) FuncType{
            {TypeKind::Func},
            std::pmr::vector<Type *>(params.begin(), params.end(), s->mr),
            ret,
        };
    }

    Type *tuple_type(Store *s, std::span<Type * const> elems) {
        void *mem = s->mr->allocate(sizeof(TupleType), alignof(TupleType));
        return ::new(mem) TupleType{
            {TypeKind::Tuple},
            std::pmr::vector<Type *>(elems.begin(), elems.end(), s->mr),
        };
    }

    Type *builtin_type(Store *s, resolver::Builtin b) {
        switch (b) {
            case resolver::Builtin::Int: return s->int_type;
            case resolver::Builtin::Float: return s->float_type;
            case resolver::Builtin::Bool: return s->bool_type;
            case resolver::Builtin::String: return s->string_type;
            case resolver::Builtin::Char: return s->char_type;
            case resolver::Builtin::Nil: return s->nil_type;
            case resolver::Builtin::Void: return s->void_type;
            case resolver::Builtin::Int8: return s->int8_type;
            case resolver::Builtin::Int16: return s->int16_type;
            case resolver::Builtin::Int32: return s->int32_type;
            case resolver::Builtin::Int64: return s->int64_type;
            case resolver::Builtin::UInt8: return s->uint8_type;
            case resolver::Builtin::UInt16: return s->uint16_type;
            case resolver::Builtin::UInt32: return s->uint32_type;
            case resolver::Builtin::UInt64: return s->uint64_type;
            case resolver::Builtin::Float32: return s->float32_type;
            case resolver::Builtin::Float64: return s->float64_type;
        }
        return s->error_type;
    }
}
