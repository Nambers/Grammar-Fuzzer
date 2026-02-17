#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <lua.hpp>
#include <fstream>
#include <iostream>
#include <sys/time.h>

using json = nlohmann::json;
using namespace FuzzingAST;

inline constexpr const char *RED   = "\033[0;31m";
inline constexpr const char *RESET = "\033[0m";

template <typename... Args>
[[noreturn]] void __attribute__((noreturn))
PANIC(std::format_string<Args...> fmt, Args &&...args) {
    std::cerr << RED << std::format(fmt, std::forward<Args>(args)...) << RESET
              << std::endl;
    abort();
}

static void runLuaStr(const std::string &code) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    struct timeval start{};
    gettimeofday(&start, nullptr);

    int ret = luaL_dostring(L, code.c_str());

    struct timeval now{};
    gettimeofday(&now, nullptr);
    int elapsed_ms = static_cast<int>(
        (now.tv_sec - start.tv_sec) * 1000 +
        (now.tv_usec - start.tv_usec) / 1000);
    std::cout << "Execution time: " << elapsed_ms << " ms\n";

    if (ret != LUA_OK) {
        if (lua_isstring(L, -1)) {
            std::cerr << "Lua error: " << lua_tostring(L, -1) << "\n";
            lua_pop(L, 1);
        }
    }
    lua_close(L);
}

int main(int argc, char *argv[]) {
    std::ifstream in("ast_test.json");
    if (!in) {
        std::cerr << "Failed to open ast_test.json\n";
        return 1;
    }
    json j;
    in >> j;

    AST ast = j.get<AST>();
    BuiltinContext ctx;
    loadBuiltinsFuncs(ctx);
    initPrimitiveTypes(ctx);

    std::string result;
    if (argc == 2 && std::string(argv[1]) == "-d") {
        std::cout << "Generated Lua declarations:\n";
        for (const auto &declID : ast.scopes[0].declarations) {
            const auto &node = ast.declarations[declID];
            if (node.kind != ASTNodeKind::Function) {
                std::ostringstream script;
                nodeToLua(script, node, ast, ctx, 0);
                std::cout << script.str();
                result += script.str();
            }
        }
    } else {
        std::ostringstream script;
        scopeToLua(script, 0, ast, ctx, 0);
        std::cout << "Generated Lua script:\n" << script.str() << "\n";
        result = script.str();
    }
    runLuaStr(result);
    return 0;
}
