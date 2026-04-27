#include <iostream>
#include <string>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

int main(int argc, char* argv[]) {
    const char* outPath = argc > 1 ? argv[1] : "builtins.json";
    const char* scriptPath = argc > 2 ? argv[2] : "builtins_gen.lua";

    lua_State* L = luaL_newstate();
    if (!L) {
        std::cerr << "Failed to create Lua state\n";
        return 1;
    }

    luaL_openlibs(L);

    // builtins_gen.lua reads arg[1] as the output path.
    lua_newtable(L);
    lua_pushstring(L, outPath);
    lua_rawseti(L, -2, 1);
    lua_setglobal(L, "arg");

    if (luaL_loadfile(L, scriptPath) != LUA_OK) {
        std::cerr << "Failed to load script: " << scriptPath << "\n";
        std::cerr << lua_tostring(L, -1) << "\n";
        lua_close(L);
        return 1;
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::cerr << "Probe script error: " << lua_tostring(L, -1) << "\n";
        lua_close(L);
        return 1;
    }

    lua_close(L);
    std::cout << "[builtins_probe] generated " << outPath << "\n";
    return 0;
}