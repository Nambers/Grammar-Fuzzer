#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <lua.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace FuzzingAST;
namespace fs = std::filesystem;

static BuiltinContext ctx;
static const fs::path queueDir = "corpus/queue";
static const fs::path doneDir  = "corpus/done";

inline constexpr const char *RED   = "\033[0;31m";
inline constexpr const char *RESET = "\033[0m";

template <typename... Args>
[[noreturn]] void __attribute__((noreturn))
PANIC(std::format_string<Args...> fmt, Args &&...args) {
    std::cerr << RED << std::format(fmt, std::forward<Args>(args)...) << RESET
              << std::endl;
    abort();
}

static std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

static void runLuaStr(lua_State *L, const std::string &code) {
    int ret = luaL_dostring(L, code.c_str());
    if (ret != LUA_OK) {
        if (lua_isstring(L, -1))
            lua_pop(L, 1);
    }
    // don't care about errors during coverage collection
}

static void collect() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    for (const auto &entry : entries) {
        const fs::path &filePath = entry.path();
        std::string filename = filePath.filename().string();
        try {
            std::string content = readFile(filePath);
            nlohmann::json jsonData = nlohmann::json::parse(content);
            AST ast = jsonData.get<AST>();

            std::ostringstream script;
            scopeToLua(script, 0, ast, ctx, 0);

            lua_State *L = luaL_newstate();
            luaL_openlibs(L);
            runLuaStr(L, script.str());
            lua_close(L);

            fs::rename(filePath, doneDir / filename);
        } catch (const std::exception &e) {
            std::cerr << "[cov] Error processing " << filename << ": "
                      << e.what() << "\n";
        }
    }
}

int main() {
    if (!fs::exists(doneDir))
        fs::create_directories(doneDir);

    loadBuiltinsFuncs(ctx);
    initPrimitiveTypes(ctx);

    std::cout << "[cov] Starting Lua coverage runner...\n";
    collect();
    return 0;
}
