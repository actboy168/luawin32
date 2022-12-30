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
        MethodDefSig signature = api->method.Signature();
        bool ok = create_caller(L, (uintptr_t)address, cache, signature);
        if (!ok) {
            return luaL_error(L, "%s has too many parameters.", name.data());
        }
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
