#pragma once

#include <stdint.h>
#include <lua.hpp>
#include <functional>
#include "cache.h"

namespace win32 {
    using fromlua_t = std::function<uintptr_t(lua_State*,int)>;
    bool create_caller(lua_State* L, uintptr_t f, win32::cache const* cache, winmd::reader::MethodDefSig const& sig);
}
