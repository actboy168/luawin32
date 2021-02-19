local lm = require "luamake"

lm.mode = "debug"

lm:lua_dll "win32" {
    includes = {
        "src/winmd"
    },
    sources = {
        "src/*.cpp"
    }
}
