#include <winmd_reader.h>
#include <lua.hpp>

using namespace winmd::reader;

struct metadata {
    struct api {
        std::string_view module;
        MethodDef        method;
        uint16_t         flags;
    };
    metadata(std::string_view const& file)
        : m_databases(file) {
        auto& db = m_databases;
        for (auto&& impl : db.ImplMap) {
            m_apis.try_emplace(impl.ImportName(), api {
                impl.ImportScope().Name(),
                impl.MemberForwarded(),
                impl.MappingFlags()
            });
        }
    }
    auto const& databases() const noexcept {
        return m_databases;
    }
    const metadata::api* find(std::string_view const& name) const noexcept {
        auto it = m_apis.find(name);
        if (it == m_apis.end()) {
            return nullptr;
        }
        return &it->second;
    }
private:
    database m_databases;
    std::map<std::string_view, api> m_apis;
};

namespace win32 {
    using namespace std::literals;

    static std::string_view lua_checkstrview(lua_State* L, int idx) {
        size_t len = 0;
        const char* str = luaL_checklstring(L, idx, &len);
        return {str, len};
    }
    
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
            if (type.ptr_count() == 1 && type.element_type() == ElementType::I1) {
                if (lua_isnoneornil(L, idx)) {
                    return 0;
                }
                return (uintptr_t)luaL_checkstring(L, idx);
            }
            return luaL_checkinteger(L, idx);
        }
    };

    template <typename>
    struct function_type_;
    template <size_t ...IS>
    struct function_type_<std::index_sequence<IS...>> {
        using type = uintptr_t (__stdcall *)(decltype(IS, uintptr_t())...);
    };
    template <size_t N>
    using function_type = typename function_type_<std::make_index_sequence<N>>::type;

    struct basic_caller {
        virtual uintptr_t call() = 0;
    };

    template <size_t N>
    struct caller : public basic_caller {
        function_type<N> f;
        const uintptr_t* params;
        caller(uintptr_t f_, const uintptr_t* params_) : f(reinterpret_cast<function_type<N>>(f_)), params(params_) {}
        uintptr_t call();
    };
    template <> uintptr_t caller<0>::call() { return f(); }
    template <> uintptr_t caller<1>::call() { return f(params[0]); }
    template <> uintptr_t caller<2>::call() { return f(params[0],params[1]); }
    template <> uintptr_t caller<3>::call() { return f(params[0],params[1],params[2]); }
    template <> uintptr_t caller<4>::call() { return f(params[0],params[1],params[2],params[3]); }
    template <> uintptr_t caller<5>::call() { return f(params[0],params[1],params[2],params[3],params[4]); }

    struct function {
        function(const metadata::api* api, void* address)
            : m_address((uintptr_t)address)
            , m_signature(api->method.Signature()) {
            m_params.resize(m_signature.ParamCount());
        }
        uintptr_t call() {
            switch (m_params.size()) {
            case 0: 
            case 1: return caller<1>{m_address, m_params.data()}.call();
            case 2: return caller<2>{m_address, m_params.data()}.call();
            case 3: return caller<3>{m_address, m_params.data()}.call();
            case 4: return caller<4>{m_address, m_params.data()}.call();
            case 5: return caller<5>{m_address, m_params.data()}.call();
            default: return 0;
            }
        }
        int run(lua_State* L) {
            int i = 1;
            for (const auto& param : m_signature.Params()) {
                m_params[i-1] = type_convert::fromlua(L, param.Type(), i);
                i++;
            }
            uintptr_t res = call();
            if (!m_signature.ReturnType()) {
                return 0;
            }
            return type_convert::tolua(L, m_signature.ReturnType().Type(), res);
        }
        static int luafunction(lua_State* L) {
            function& f = *(function*)lua_touserdata(L, lua_upvalueindex(1));
            return f.run(L);
        }
        static void push(lua_State* L, const metadata::api* api, void* address) {
            lua_pushlightuserdata(L, (void*)new function(api, address));
            lua_pushcclosure(L, luafunction, 1);
        }
        uintptr_t              m_address;
        std::vector<uintptr_t> m_params;
        MethodDefSig           m_signature;
    };

    static int apis_get(lua_State* L) {
        static metadata metadata("Windows.Win32.winmd"sv);
        static native_apis native_apis;

        auto name = lua_checkstrview(L, 2);
        auto api = metadata.find(name);
        if (!api) {
            return luaL_error(L, "%s not found.", name.data());
        }
        void* address = native_apis.find(api->module, name);
        if (!address) {
            return luaL_error(L, "%s can't load.", name.data());
        }
        auto ignature = api->method.Signature();
        function::push(L, api, address);
        lua_pushvalue(L, -1);
        lua_insert(L, 2);
        lua_rawset(L, -4);
        return 1;
    }
    static int open_apis(lua_State* L) {
        lua_newtable(L);
        static luaL_Reg mt[] = {
            { "__index", apis_get },
            { NULL, NULL },
        };
        luaL_newlib(L, mt);
        lua_setmetatable(L, -2);
        return 1;
    }
    static int open(lua_State* L) {
        static luaL_Reg l[] = {
            { NULL, NULL },
        };
        luaL_newlib(L, l);
        lua_pushstring(L, "apis");
        open_apis(L);
        lua_rawset(L, -3);
        return 1;
    }
}

int luaopen_win32(lua_State* L) {
    return win32::open(L);
}
