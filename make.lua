local lm = require "luamake"

lm.mode = "debug"
lm.cxx = "c++17"

lm:lua_dll "win32" {
    includes = {
        "src/winmd"
    },
    sources = {
        "src/*.cpp"
    }
}
