#include "ast.hpp"
#include "driver.hpp"
#include "dumper.hpp"
#include "serialization.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

extern "C" {
#include <quickjs.h>
}

using namespace FuzzingAST;
namespace fs = std::filesystem;

static const fs::path queueDir = "corpus/queue";
static const fs::path doneDir = "corpus/done";

static std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("Cannot open file: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

static void runJSStr(JSContext *jctx, const std::string &code) {
    JSValue ret =
        JS_Eval(jctx, code.c_str(), code.size(), "<cov>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ret)) {
        JSValue exc = JS_GetException(jctx);
        JS_FreeValue(jctx, exc);
    }
    JS_FreeValue(jctx, ret);
}

static void collect() {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(queueDir)) {
        if (entry.is_regular_file())
            entries.push_back(entry);
    }

    JSRuntime *rt = JS_NewRuntime();
    JSContext *jctx = JS_NewContext(rt);

    for (const auto &entry : entries) {
        const fs::path &filePath = entry.path();
        std::string filename = filePath.filename().string();
        try {
            std::string content = readFile(filePath);
            nlohmann::json jsonData = nlohmann::json::parse(content);
            AST ast = jsonData.get<AST>();

            std::ostringstream script;
            scopeToJS(script, 0, ast, 0);

            runJSStr(jctx, script.str());
            fs::rename(filePath, doneDir / filename);
        } catch (const std::exception &e) {
            std::cerr << "[cov] Error processing " << filename << ": "
                      << e.what() << "\n";
        }
    }

    JS_FreeContext(jctx);
    JS_FreeRuntime(rt);
}

int main() {
    if (!fs::exists(doneDir))
        fs::create_directories(doneDir);

    std::cout << "[cov] Starting QuickJS coverage runner...\n";
    collect();
    return 0;
}
