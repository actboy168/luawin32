#include <winmd_reader.h>
#include <lua.hpp>
#include "caller.h"

using namespace winmd::reader;

namespace win32 {
    using namespace std::literals;

    static std::string_view lua_checkstrview(lua_State* L, int idx) {
        size_t len = 0;
        const char* str = luaL_checklstring(L, idx, &len);
        return {str, len};
    }

    template <typename T>
    const T* tablefind(std::map<std::string_view, T> const& m, std::string_view const& name) noexcept {
        auto it = m.find(name);
        if (it == m.end()) {
            return nullptr;
        }
        return &it->second;
    }

    struct api_t {
        std::string_view module;
        MethodDef        method;
        uint16_t         flags;
    };
    struct apis_t : public std::map<std::string_view, api_t> {
        apis_t(database const& db){
            for (auto&& impl : db.ImplMap) {
                try_emplace(impl.ImportName(), api_t {
                    impl.ImportScope().Name(),
                    impl.MemberForwarded(),
                    impl.MappingFlags()
                });
            }
        }
    };

    struct constant_t {
        Constant value;
    };
    struct constants_t : public std::map<std::string_view, constant_t> {
        constants_t(database const& db){
            for (auto&& field : db.Field) {
                try_emplace(field.Name(), constant_t {
                    field.Constant()
                });
            }
        }
    };

    class native_apis {
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
        static uintptr_t fromlua(lua_State* L, TypeSig type, int idx) {
            if (type.ptr_count() > 0) {
                switch (lua_type(L, idx)) {
                case LUA_TNONE:
                case LUA_TNIL:
                    return 0;
                case LUA_TSTRING:
                    return (uintptr_t)luaL_checkstring(L, idx);
                default:
                    luaL_error(L, "#%d cannot be converted to pointer.", idx);
                    return 0;
                }
            }
            return luaL_checkinteger(L, idx);
        }
    };

    struct function {
        function(const api_t* api, void* address)
            : m_signature(api->method.Signature())
            , m_caller(caller::create((uintptr_t)address, m_signature.ParamCount()))
        { }
        int run(lua_State* L) {
            if (!m_caller) {
                return 0;
            }
            int i = 0;
            for (const auto& param : m_signature.Params()) {
                m_caller->set(i, type_convert::fromlua(L, param.Type(), i + 1));
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
        static void push(lua_State* L, const api_t* api, void* address) {
            lua_pushlightuserdata(L, (void*)new function(api, address));
            lua_pushcclosure(L, luafunction, 1);
        }
        MethodDefSig m_signature;
        caller*      m_caller;
    };

    static int apis_get(lua_State* L) {
        static native_apis native_apis;
        auto apis = (const apis_t*)lua_touserdata(L, lua_upvalueindex(1));
        auto name = lua_checkstrview(L, 2);
        auto api = tablefind(*apis, name);
        if (!api) {
            return luaL_error(L, "%s not found.", name.data());
        }
        void* address = native_apis.find(api->module, name);
        if (!address) {
            return luaL_error(L, "%s can't load.", name.data());
        }
        function::push(L, api, address);
        lua_pushvalue(L, -1);
        lua_insert(L, 2);
        lua_rawset(L, -4);
        return 1;
    }
    static int init_apis(lua_State* L, database const& db) {
        static apis_t apis(db);
        lua_newtable(L);
        static luaL_Reg mt[] = {
            { "__index", apis_get },
            { NULL, NULL },
        };
        luaL_newlibtable(L, mt);
        lua_pushlightuserdata(L, (void*)&apis);
        luaL_setfuncs(L, mt, 1);
        lua_setmetatable(L, -2);
        return 1;
    }
    static int constants_get(lua_State* L) {
        auto constants = (const constants_t*)lua_touserdata(L, lua_upvalueindex(1));
        auto name = lua_checkstrview(L, 2);
        auto constant = tablefind(*constants, name);
        if (!constant) {
            return luaL_error(L, "%s not found.", name.data());
        }
        auto value = constant->value;
        switch (value.Type()) {
        case ConstantType::Boolean:
            lua_pushboolean(L, value.ValueBoolean());
            break;
        case ConstantType::Char:
            lua_pushinteger(L, value.ValueChar());
            break;
        case ConstantType::Int8:
            lua_pushinteger(L, value.ValueInt8());
            break;
        case ConstantType::UInt8:
            lua_pushinteger(L, value.ValueUInt8());
            break;
        case ConstantType::Int16:
            lua_pushinteger(L, value.ValueInt16());
            break;
        case ConstantType::UInt16:
            lua_pushinteger(L, value.ValueUInt16());
            break;
        case ConstantType::Int32:
            lua_pushinteger(L, value.ValueInt32());
            break;
        case ConstantType::UInt32:
            lua_pushinteger(L, value.ValueUInt32());
            break;
        case ConstantType::Int64:
            lua_pushinteger(L, value.ValueInt64());
            break;
        case ConstantType::UInt64:
            lua_pushinteger(L, value.ValueUInt64());
            break;
        case ConstantType::Float32:
            lua_pushnumber(L, value.ValueFloat32());
            break;
        case ConstantType::Float64:
            lua_pushnumber(L, value.ValueFloat64());
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
    static int init_constants(lua_State* L, database const& db) {
        static constants_t constants(db);
        lua_newtable(L);
        static luaL_Reg mt[] = {
            { "__index", constants_get },
            { NULL, NULL },
        };
        luaL_newlibtable(L, mt);
        lua_pushlightuserdata(L, (void*)&constants);
        luaL_setfuncs(L, mt, 1);
        lua_setmetatable(L, -2);
        return 1;
    }
    static int init_version(lua_State* L, database const& db) {
        auto version = db.Assembly[0].Version();
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
        static database db("Windows.Win32.winmd"sv);
        struct {
            const char* name;
            int (*func)(lua_State* L, database const& db);
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
