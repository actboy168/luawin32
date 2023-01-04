#pragma once

#include <stdint.h>
#include <lua.hpp>
#include <functional>
#include "cache.h"

namespace win32 {
    bool create_caller(lua_State* L, uintptr_t f, win32::cache const* cache, winmd::reader::MethodDef const& method);
}
