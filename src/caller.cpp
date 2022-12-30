#include "caller.h"
#include <array>
#include <utility>
#include "cache.h"

using namespace winmd::reader;

namespace win32 {
    
    static TypeDef resolve_type(win32::cache const& cache, coded_index<TypeDefOrRef> const& type_index) {
        if (type_index.type() == TypeDefOrRef::TypeDef) {
            return type_index.TypeDef();
        }
        auto const& typeref = type_index.TypeRef();
        return cache.find_required(typeref.TypeNamespace(), typeref.TypeName());
    };

    fromlua_t fromlua_invalid = [](lua_State*,int) { return 0; };
    fromlua_t fromlua_void = [](lua_State*,int) { return 0; };
    fromlua_t fromlua_int  = [](lua_State* L,int idx) { return luaL_checkinteger(L, idx); };

    std::map<std::string_view, fromlua_t> FromLua = {
        { "HWND", [](lua_State* L, int idx)->uintptr_t {
            return 0;
        }},
        { "PSTR", [](lua_State* L, int idx)->uintptr_t {
            return (uintptr_t)luaL_checkstring(L, idx);
        }}
    };

    static fromlua_t fromlua(lua_State* L, const win32::cache* cache, TypeSig type, int idx) {
        assert(type.ptr_count() == 0);
        switch (type.element_type()) {
        case ElementType::Void:
            return fromlua_void;
        case ElementType::ValueType: {
            auto& type_index = std::get<coded_index<TypeDefOrRef>>(type.Type());
            auto def = resolve_type(*cache, type_index);
            auto name = def.TypeName();
            if (def.is_enum()) {
                auto const& enum_def = def.get_enum_definition();
                switch (enum_def.m_underlying_type) {
                case ElementType::I1:
                case ElementType::U1:
                case ElementType::I2:
                case ElementType::U2:
                case ElementType::I4:
                case ElementType::U4:
                case ElementType::I8:
                case ElementType::U8:
                case ElementType::U:
                case ElementType::I:
                    return fromlua_int;
                default:
                    break;
                }
                luaL_error(L, "#%d Unrecognized %s encountered.", idx, name.data());
                return fromlua_invalid;
            }
            auto it = FromLua.find(name);
            if (it == FromLua.end()) {
                luaL_error(L, "#%d Unrecognized %s encountered.", idx, name.data());
                return fromlua_invalid;
            }
            return it->second;
        }
        case ElementType::Boolean:
        case ElementType::Char:
        case ElementType::I1:
        case ElementType::U1:
        case ElementType::I2:
        case ElementType::U2:
        case ElementType::I4:
        case ElementType::U4:
        case ElementType::I8:
        case ElementType::U8:
        case ElementType::R4:
        case ElementType::R8:
        case ElementType::U:
        case ElementType::I:
        case ElementType::String:
        case ElementType::Object:
        case ElementType::GenericInst:
        case ElementType::Class:
        case ElementType::Var:
        case ElementType::MVar:
        default:
            luaL_error(L, "#%d Unrecognized ELEMENT_TYPE encountered.", idx);
            return fromlua_invalid;
        }
    }

    template <typename>
    struct function_type_;
    template <size_t ...Is>
    struct function_type_<std::index_sequence<Is...>> {
        using type = uintptr_t (__stdcall *)(decltype(Is, uintptr_t())...);
    };
    template <size_t N>
    using function_type = typename function_type_<std::make_index_sequence<N>>::type;

    template <size_t N>
    struct caller {
        function_type<N> f;
        std::array<fromlua_t, N> params;
        caller(uintptr_t f_)
            : f(reinterpret_cast<function_type<N>>(f_))
            , params()
        {}
        void set_param(size_t i, fromlua_t f) {
            params[i] = f;
        }
        template <size_t ...Is>
        uintptr_t call_impl(lua_State* L, std::index_sequence<Is...>) const {
            return f(params[Is](L, Is+1)...);
        }
        static int s_call(lua_State* L) {
            caller& c = *(caller*)lua_touserdata(L, lua_upvalueindex(1));
            c.call_impl(L, std::make_index_sequence<N>());
            return 0;
        }
        static void create(lua_State* L, uintptr_t f, win32::cache const* cache, winmd::reader::MethodDefSig const& sig) {
            caller* c = (caller*)lua_newuserdatauv(L, sizeof(caller), 0);
            new (c) caller{f};
            int i = 1;
            for (const auto& param : sig.Params()) {
                auto f = fromlua(L, cache, param.Type(), i);
                c->set_param(i-1, f);
                i++;
            }
            lua_pushcclosure(L, s_call, 1);
        }
    };

    bool create_caller(lua_State* L, uintptr_t f, win32::cache const* cache, winmd::reader::MethodDefSig const& sig) {
        switch (sig.ParamCount()) {
        case 0: caller<0>::create(L, f, cache, sig); return true;
        case 1: caller<1>::create(L, f, cache, sig); return true;
        case 2: caller<2>::create(L, f, cache, sig); return true;
        case 3: caller<3>::create(L, f, cache, sig); return true;
        case 4: caller<4>::create(L, f, cache, sig); return true;
        case 5: caller<5>::create(L, f, cache, sig); return true;
        default: return false;
        }
    }
}
