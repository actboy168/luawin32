#include <winmd_reader.h>
#include <lua.hpp>
#include "caller.h"
#include "cache.h"

using namespace winmd::reader;

namespace win32 {
    using namespace std::literals;

    static std::string_view lua_checkstrview(lua_State* L, int idx) {
        size_t len = 0;
        const char* str = luaL_checklstring(L, idx, &len);
        return {str, len};
    }

    static TypeDef resolve_type(win32::cache const& cache, coded_index<TypeDefOrRef> const& type_index) {
        if (type_index.type() == TypeDefOrRef::TypeDef) {
            return type_index.TypeDef();
        }
        auto const& typeref = type_index.TypeRef();
        return cache.find_required(typeref.TypeNamespace(), typeref.TypeName());
    };

    std::map<std::string_view, std::function<uintptr_t(lua_State*,int)>> FromLua = {
        { "HWND", [](lua_State* L, int idx)->uintptr_t {
            return 0;
        }},
        { "PSTR", [](lua_State* L, int idx)->uintptr_t {
            return (uintptr_t)luaL_checkstring(L, idx);
        }}
    };

    class native_modules {
    public:
        void* find(std::string_view module, std::string_view api) {
            HMODULE dll = find_module(module);
            if (!dll) {
                return nullptr;
            }
            return (void*)GetProcAddress(dll, api.data());
        }
    private:
        HMODULE find_module(std::string_view module) {
            auto it = m_modules.find(module);
            if (it != m_modules.end()) {
                return it->second;
            }
            HMODULE dll = LoadLibraryA(module.data());
            m_modules.try_emplace(module, dll);
            return dll;
        }
        std::map<std::string_view, HMODULE> m_modules;
    };

    class type_convert {
    public:
        static int tolua(lua_State* L, TypeSig type, uintptr_t v) {
            return 0;
        }
        static uintptr_t fromlua(lua_State* L, const win32::cache* cache, TypeSig type, int idx) {
            assert(type.ptr_count() == 0);
            switch (type.element_type()) {
            case ElementType::Void:
                return 0;
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
                        return luaL_checkinteger(L, idx);
                    default:
                        luaL_error(L, "#%d Unrecognized %s encountered.", idx, name.data());
                        return 0;
                    }
                    return 0;
                }
                auto it = FromLua.find(name);
                if (it == FromLua.end()) {
                    luaL_error(L, "#%d Unrecognized %s encountered.", idx, name.data());
                    return 0;
                }
                return it->second(L, idx);
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
                return 0;
            }
        }
    };

    struct function {
        function(const win32::cache* cache, const win32::api_t* api, void* address)
            : m_cache(cache)
            , m_signature(api->method.Signature())
            , m_caller(caller::create((uintptr_t)address, m_signature.ParamCount()))
        { }
        int run(lua_State* L) {
            if (!m_caller) {
                return 0;
            }
            int i = 0;
            for (const auto& param : m_signature.Params()) {
                m_caller->set(i, type_convert::fromlua(L, m_cache, param.Type(), i + 1));
                i++;
            }
            uintptr_t res = m_caller->call();
            if (!m_signature.ReturnType()) {
                return 0;
            }
            return type_convert::tolua(L, m_signature.ReturnType().Type(), res);
        }
        static int luafunction(lua_State* L) {
            function& f = *(function*)lua_touserdata(L, lua_upvalueindex(1));
            return f.run(L);
        }
        static void push(lua_State* L, const win32::cache* cache, const win32::api_t* api, void* address) {
            lua_pushlightuserdata(L, (void*)new function(cache, api, address));
            lua_pushcclosure(L, luafunction, 1);
        }
        const win32::cache* m_cache;
        MethodDefSig m_signature;
        caller*      m_caller;
    };

    static int apis_get(lua_State* L) {
        static native_modules native_apis;
        auto cache = (const win32::cache*)lua_touserdata(L, lua_upvalueindex(1));
        auto name = lua_checkstrview(L, 2);
        auto api = cache->find_api(name);
        if (!api) {
            return luaL_error(L, "%s not found.", name.data());
        }
        void* address = native_apis.find(api->module, name);
        if (!address) {
            return luaL_error(L, "%s can't load.", name.data());
        }
        function::push(L, cache, api, address);
        lua_pushvalue(L, -1);
        lua_insert(L, 2);
        lua_rawset(L, -4);
        return 1;
    }
    static int init_apis(lua_State* L, win32::cache const& cache) {
        lua_newtable(L);
        static luaL_Reg mt[] = {
            { "__index", apis_get },
            { NULL, NULL },
        };
        luaL_newlibtable(L, mt);
        lua_pushlightuserdata(L, (void*)&cache);
        luaL_setfuncs(L, mt, 1);
        lua_setmetatable(L, -2);
        return 1;
    }
    static int constants_get(lua_State* L) {
        auto cache = (const win32::cache*)lua_touserdata(L, lua_upvalueindex(1));
        auto name = lua_checkstrview(L, 2);
        auto constant = cache->find_constant(name);
        if (!constant) {
            return luaL_error(L, "%s not found.", name.data());
        }
        switch (constant->Type()) {
        case ConstantType::Boolean:
            lua_pushboolean(L, constant->ValueBoolean());
            break;
        case ConstantType::Char:
            lua_pushinteger(L, constant->ValueChar());
            break;
        case ConstantType::Int8:
            lua_pushinteger(L, constant->ValueInt8());
            break;
        case ConstantType::UInt8:
            lua_pushinteger(L, constant->ValueUInt8());
            break;
        case ConstantType::Int16:
            lua_pushinteger(L, constant->ValueInt16());
            break;
        case ConstantType::UInt16:
            lua_pushinteger(L, constant->ValueUInt16());
            break;
        case ConstantType::Int32:
            lua_pushinteger(L, constant->ValueInt32());
            break;
        case ConstantType::UInt32:
            lua_pushinteger(L, constant->ValueUInt32());
            break;
        case ConstantType::Int64:
            lua_pushinteger(L, constant->ValueInt64());
            break;
        case ConstantType::UInt64:
            lua_pushinteger(L, constant->ValueUInt64());
            break;
        case ConstantType::Float32:
            lua_pushnumber(L, constant->ValueFloat32());
            break;
        case ConstantType::Float64:
            lua_pushnumber(L, constant->ValueFloat64());
            break;
        case ConstantType::String:
        case ConstantType::Class:
        default:
            //TODO
            lua_pushnil(L);
            break;
        }
        lua_pushvalue(L, -1);
        lua_insert(L, 2);
        lua_rawset(L, -4);
        return 1;
    }
    static int init_constants(lua_State* L, win32::cache const& cache) {
        lua_newtable(L);
        static luaL_Reg mt[] = {
            { "__index", constants_get },
            { NULL, NULL },
        };
        luaL_newlibtable(L, mt);
        lua_pushlightuserdata(L, (void*)&cache);
        luaL_setfuncs(L, mt, 1);
        lua_setmetatable(L, -2);
        return 1;
    }
    static int init_version(lua_State* L, win32::cache const& cache) {
        auto version = cache.database().Assembly[0].Version();
        lua_newtable(L);
        lua_pushinteger(L, version.MajorVersion);
        lua_setfield(L, -2, "MajorVersion");
        lua_pushinteger(L, version.MinorVersion);
        lua_setfield(L, -2, "MinorVersion");
        lua_pushinteger(L, version.BuildNumber);
        lua_setfield(L, -2, "BuildNumber");
        lua_pushinteger(L, version.RevisionNumber);
        lua_setfield(L, -2, "RevisionNumber");
        return 1;
    }
    static int open(lua_State* L) {
        static win32::cache db("Windows.Win32.winmd"sv);
        struct {
            const char* name;
            int (*func)(lua_State* L, win32::cache const& db);
        } init[] = {
            { "apis", init_apis },
            { "constants", init_constants },
            { "version", init_version },
            { NULL, NULL },
        };
        lua_newtable(L);
        for (auto l = init; l->name != NULL; l++) {
            l->func(L, db);
            lua_setfield(L, -2, l->name);
        }
        return 1;
    }
}

int luaopen_win32(lua_State* L) {
    return win32::open(L);
}
