local lm = require "luamake"

lm.mode = "debug"

lm:lua_dll "win32" {
    includes = {
        "winmd"
    },
    sources = {
        "win32.cpp"
    }
}
